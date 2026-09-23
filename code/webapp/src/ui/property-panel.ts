/**
 * The property inspector (FR-22): geometry, data binding, formatting, font and alert rules for
 * the selected widget.
 *
 * THE PANEL EDITS THE WIDGET IN PLACE AND CALLS BACK. It does not hold a copy, and it does not
 * save. The shell owns the document and the device write; this only mutates the widget it was
 * given and reports the change, so there is exactly one writer of the config and no way for the
 * panel and the canvas editor to disagree about what the current state is.
 *
 * EVERY CONTROL WRITES THROUGH. A field that shows a value but does not store it is the exact
 * class of bug this project has hit repeatedly — the UI confirms a change that has no effect.
 * So each input handler assigns to the widget before notifying.
 */

import {
  MAX_ALERT_RULES_PER_WIDGET,
  type AlertLevel, type AlertOp, type AlertRule, type DataBinding, type DataSourceKind,
  type Label, type Page, type Rule, type Selection, type Widget,
} from '../model/config';
import { describeRule } from '../alerts/rules';
import { describeBinding } from '../data/binding';
import { formatPlaceholder } from '../data/format';
import { FACES_AVAILABLE, sizeFor, sizeForPx } from '../canvas/face';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../model/canvas-consts';

export interface PropertyPanelOptions {
  host: HTMLElement;
  /** Called after any edit to a value box, with the widget that changed. */
  onChange: (w: Widget) => void;
  /** Called after any edit to a divider. The rules are baked into the static layer, so the
   *  shell must rebuild it — a panel that edited the number without repainting would confirm a
   *  move the preview never shows. */
  onRuleChange: () => void;
  /** Called after any edit to a label. Same reason as onRuleChange: labels are baked into the
   *  static layer, so a renamed or moved heading must repaint that layer or the preview would
   *  show the old one while the document held the new. */
  onLabelChange: () => void;
  /** Called when the user deletes the current selection. */
  onDelete: (sel: Selection) => void;
  /** Entity ids to offer in the picker, if a list has been fetched. Empty is fine. */
  entities?: { entityId: string; friendlyName?: string }[];
  /** Reports what an entity id looks like, so a typo is caught as it is typed. */
  onValidateEntity?: (id: string) => void;
  /** Ask for entities matching `q`. The list cannot be preloaded: Home Assistant sends no CORS
   *  headers, so the browser cannot fetch it, and the device-side search returns a bounded subset
   *  for a term rather than the whole instance. The shell debounces and calls back with results. */
  onEntitySearch?: (q: string) => void;
  /** True when the HA entity list could not be fetched, so offer free text instead. */
  entitiesUnavailable?: string;
}

export interface PropertyPanelHandle {
  /** Re-render for a different selection, or for no selection. */
  show(sel: Selection | undefined, page: Page): void;
  /** Replace the entity list after an async fetch completes. */
  setEntities(list: { entityId: string; friendlyName?: string }[], unavailable?: string): void;
  /** Replace ONLY the picker's options, WITHOUT re-rendering. This is what a search-as-you-type
   *  result needs: a full re-render would replace the input the user is typing in and drop focus
   *  after one keystroke (see the geometry note in render()).
   *
   *  `note` is a failure message to show instead of a match count. `total` is how many entities
   *  matched on the display, which can exceed the rows returned — saying so keeps a clipped list
   *  from reading as the whole set. */
  setEntityOptions(list: { entityId: string; friendlyName?: string }[], note?: string, total?: number): void;
}

/** A button with a real listener. `onClick` in a props object does NOT work: Object.assign
 *  sets an `onClick` property that is not the DOM's `onclick`, so the handler is silently
 *  dropped and the button does nothing. */
function makeButton(label: string, onClick: () => void): HTMLButtonElement {
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = label;
  b.addEventListener('click', onClick);
  return b;
}

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: Partial<HTMLElementTagNameMap[K]> = {},
  ...children: (Node | string)[]
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  Object.assign(node, props);
  for (const c of children) node.append(c);
  return node;
}

/* THE VALUE MENU MUST INCLUDE `icon`.
 *
 * It did not, and that is the reported "the icons don't make sense, I don't know how to add more":
 * the shipped layout has one icon box, but the only way to create another is to bind a widget to
 * the `icon` field — and the dropdown could not produce that binding at all, so the icon was
 * unreachable from the editor. The list is now labelled rather than showing raw schema keys, so
 * "Weather icon" says what it does; the schema value is still the key. */
const OWM_FIELDS: { value: NonNullable<DataBinding['owmField']>; label: string }[] = [
  { value: 'temp', label: 'Temperature' },
  { value: 'min', label: 'Daily low' },
  { value: 'max', label: 'Daily high' },
  { value: 'wind', label: 'Wind speed' },
  { value: 'humidity', label: 'Humidity' },
  { value: 'condition', label: 'Conditions (word)' },
  { value: 'icon', label: 'Weather icon (picture)' },
  { value: 'city', label: 'Location name' },
];

/** The `icon` value is special: the box draws a PICTURE from the icon code rather than the code as
 *  text (see canvas/widget-kind.ts). The panel says so, because a user who picked it and saw "01n"
 *  in the box would otherwise have no way to know a picture will appear on the glass. */
const ICON_FIELD: NonNullable<DataBinding['owmField']> = 'icon';
const KINDS: { value: DataSourceKind; label: string }[] = [
  { value: 'owm-current', label: 'Current weather' },
  { value: 'owm-daily', label: 'Forecast' },
  { value: 'owm-alert', label: 'Weather alerts' },
  { value: 'ha', label: 'Home Assistant entity' },
];
const OPS: AlertOp[] = ['gt', 'gte', 'lt', 'lte', 'eq', 'ne'];
const LEVELS: Exclude<AlertLevel, 'none'>[] = ['advisory', 'warning', 'severe'];

export function createPropertyPanel(opts: PropertyPanelOptions): PropertyPanelHandle {
  const { host, onChange, onRuleChange, onLabelChange, onDelete } = opts;
  let entities = opts.entities ?? [];
  let entitiesUnavailable = opts.entitiesUnavailable;
  let current: Widget | undefined;
  let currentSel: Selection | undefined;
  let currentPage: Page | undefined;
  /* The live HA datalist, when an HA-bound widget is selected. Held so a search result can be
   * dropped into it without re-rendering — see setEntityOptions(). */
  let haDatalist: HTMLDataListElement | null = null;
  /* The line beside the entity field that a search updates in place. */
  let haNote: HTMLElement | null = null;

  /** Assign then notify — see the note at the top of the file. */
  function commit(patch: Partial<Widget>): void {
    if (!current) return;
    Object.assign(current, patch);
    onChange(current);
  }

  /** Edit the selected rule in place and notify. Rules are `{y, thickness, inset}` and every
   *  field is a plain number, so there is nothing to merge. */
  function commitRule(patch: Partial<Rule>): void {
    if (currentSel?.kind !== 'rule' || !currentPage?.rules) return;
    const r = currentPage.rules[currentSel.index];
    if (!r) return;
    Object.assign(r, patch);
    onRuleChange();
  }

  /** Edit the selected label in place and notify. Like commitRule, every field is a primitive, so
   *  there is nothing to merge — and it reads through currentPage so a page switch cannot leave
   *  the handler writing to a label of the previous page. */
  function commitLabel(patch: Partial<Label>): void {
    if (currentSel?.kind !== 'label' || !currentPage?.labels) return;
    const l = currentPage.labels[currentSel.index];
    if (!l) return;
    Object.assign(l, patch);
    onLabelChange();
  }

  function field(label: string, control: HTMLElement): HTMLElement {
    const id = `p-${Math.random().toString(36).slice(2, 8)}`;
    control.id = id;
    return el('div', { className: 'pRow' }, el('label', { htmlFor: id }, label), control);
  }

  /** A number field that reads back the STORED value when it loses focus.
   *
   * WHY THE READ-BACK: the handler may clamp the number it is given (a width cannot be 0, a
   * divider cannot sit below the panel). Without a read-back the field would go on showing what
   * was typed — "0" — while the widget held 1, and the next render would silently correct it to
   * a different number than the user last saw. That is the "control shows a value it did not
   * store" defect class, just in the opposite direction. The field is NOT re-rendered on every
   * keystroke, because rebuilding the DOM mid-typing drops focus after one digit (see the
   * geometry note in render()). */
  function numberInput(
    value: number,
    onInput: (n: number) => void,
    step = 1,
    readBack?: () => number,
  ): HTMLInputElement {
    const i = el('input', { type: 'number', value: String(value), step: String(step) }) as HTMLInputElement;
    i.addEventListener('input', () => {
      const n = Number(i.value);
      /* A blank or half-typed number must not be written as NaN: the field is mid-edit and
       * NaN would propagate into the geometry and the config. */
      if (Number.isFinite(n)) onInput(n);
    });
    if (readBack) {
      i.addEventListener('blur', () => { i.value = String(readBack()); });
    }
    return i;
  }

  function textInput(value: string, onInput: (s: string) => void): HTMLInputElement {
    const i = el('input', { type: 'text', value }) as HTMLInputElement;
    i.addEventListener('input', () => onInput(i.value));
    return i;
  }

  function select<T extends string>(
    value: T,
    options: { value: T; label: string }[],
    onPick: (v: T) => void,
  ): HTMLSelectElement {
    const s = el('select') as HTMLSelectElement;
    for (const o of options) {
      const opt = el('option', { value: o.value }, o.label) as HTMLOptionElement;
      if (o.value === value) opt.selected = true;
      s.append(opt);
    }
    s.addEventListener('change', () => onPick(s.value as T));
    return s;
  }

  function render(): void {
    host.replaceChildren();
    /* The previous datalist is gone with the children, so drop the stale handle: leaving it set
     * would have a search result appended to a detached node. */
    haDatalist = null;
    haNote = null;

    if (currentSel?.kind === 'rule') {
      const r = currentPage?.rules?.[currentSel.index];
      if (!r) {
        host.append(el('div', { className: 'empty' }, 'Nothing selected.'));
        return;
      }
      host.append(el('h3', {}, 'Divider'));
      const geo = el('div', { className: 'pGrid' });
      /* Captured so the blur read-back below does not have to re-narrow the selection. */
      const ruleIndex = currentSel.index;
      for (const [label, key, min] of [['Y', 'y', 0], ['Thickness', 'thickness', 1], ['Inset', 'inset', 0]] as const) {
        geo.append(
          el('div', {},
             el('label', {}, label),
             numberInput(r[key], (n) => {
               const clampMin = Math.max(min, Math.round(n));
               /* Thickness and inset are bounded by the panel; y must leave its own line on the
                * glass, so it stops one pixel short of the bottom edge. */
               const v = key === 'y' ? Math.min(679, clampMin)
                       : key === 'inset' ? Math.min(Math.floor(PANEL_WIDTH / 2), clampMin)
                       : Math.min(20, clampMin);
               /* NO render() — see the geometry note above: rebuilding the DOM mid-typing drops
                * focus after one keystroke. onRuleChange already repaints the canvas, which is
                * the only thing that depends on this value. */
               commitRule({ [key]: v } as Partial<Rule>);
             }, 1, () => currentPage?.rules?.[ruleIndex]?.[key] ?? 0)),
        );
      }
      host.append(el('fieldset', {}, el('legend', {}, 'Divider position'), geo,
        el('p', { className: 'hint' }, 'Drag the line on the panel to move it vertically.')));
      host.append(el('div', { className: 'actions' },
        makeButton('Delete divider', () => onDelete(currentSel!))));
      return;
    }

    if (currentSel?.kind === 'label') {
      const l = currentPage?.labels?.[currentSel.index];
      if (!l) {
        host.append(el('div', { className: 'empty' }, 'Nothing selected.'));
        return;
      }
      host.append(el('h3', {}, `Heading: ${l.text || '(empty)'}`));
      const idx = currentSel.index;
      const geo = el('div', { className: 'pGrid' });
      /* X AND Y ONLY AS NUMBERS. The SIZE is a face-ladder select, exactly like a widget's —
       * the device rasterises glyphs at the ladder's fixed sizes (FR-4a) and a label's size is
       * resolved to the nearest face, so a free number would be a control mostly doing nothing and
       * the displayed number would not be the size actually drawn. */
      for (const [label, key, min] of [['X', 'x', 0], ['Y', 'y', 0]] as const) {
        geo.append(
          el('div', {},
             el('label', {}, label),
             numberInput(l[key] as number, (n) => {
               const clampMin = Math.max(min, Math.round(n));
               const v = key === 'y' ? Math.min(PANEL_HEIGHT - 1, clampMin)
                       : Math.min(PANEL_WIDTH - 1, clampMin);
               commitLabel({ [key]: v } as Partial<Label>);
             }, 1, () => (currentPage?.labels?.[idx]?.[key] as number) ?? 0)),
        );
      }
      geo.append(
        el('div', {},
           el('label', {}, 'Size'),
           /* THE SELECTED VALUE IS THE FACE ACTUALLY DRAWN. A label stores a pixel size; the
            * device resolves it to the nearest ladder face. Showing the raw stored number would
            * let the field read "48 px" while the panel drew the 54 px face — a control showing a
            * value that is not what happens, which is the defect class this panel keeps hitting.
            * FACES[faceIdForPx(n)].px is exactly the size the renderer will use. */
           select(String(sizeForPx(l.font)),
             FACES_AVAILABLE.map((f) => ({ value: String(f.px), label: f.label })),
             (v) => commitLabel({ font: Number(v) }))),
      );
      host.append(el('fieldset', {}, el('legend', {}, 'Heading position and size'), geo,
        el('p', { className: 'hint' }, 'Drag the heading on the panel to move it.')));
      /* THE TEXT FIELD IS UPDATED IN PLACE, NO re-render — rebuilding the DOM mid-typing drops
       * focus after one keystroke (the same trap as every other text field here). The document is
       * updated on each keystroke, and onLabelChange repaints the canvas, which is the only other
       * thing that depends on it. */
      const tset = el('fieldset', {}, el('legend', {}, 'Heading text'));
      tset.append(field('Text', textInput(l.text, (s) => commitLabel({ text: s }))),
        el('p', { className: 'hint' },
           'These are the headings that sit above the readings, like NOW or HALLWAY.'),
        el('p', { className: 'hint' },
           'A heading is part of the picture on the display, so it appears after you press Save.'));
      host.append(tset);
      host.append(el('div', { className: 'actions' },
        makeButton('Delete heading', () => onDelete(currentSel!))));
      return;
    }

    if (!current) {
      host.append(el('div', { className: 'empty' },
        'Nothing selected. Click a value box on the panel to move, resize or bind it.'));
      return;
    }
    const w = current;

    host.append(el('h3', {}, `Box: ${w.id}`));

    /* ---- geometry (numeric entry, in addition to dragging) ---- */
    /* NO render() IN THESE HANDLERS. Re-rendering rebuilds the DOM, which REPLACES the input the
     * user is typing in — so the element loses focus after the first keystroke and the rest of
     * the digits go nowhere. Confirmed in a browser: typing "140" into X left "1" and focus on
     * the body. Nothing else on the panel depends on a geometry value, so the field the user is
     * editing is already the right display; only the canvas needs repainting, and onChange does
     * that. */
    const geo = el('div', { className: 'pGrid' });
    for (const [label, key] of [['X', 'x'], ['Y', 'y'], ['W', 'w'], ['H', 'h']] as const) {
      geo.append(
        el('div', {},
           el('label', {}, label),
           numberInput(w[key], (n) => {
             /* Geometry is clamped on the device side too, but clamping here keeps the
              * number the user sees equal to the number stored. */
             commit({ [key]: Math.max(key === 'w' || key === 'h' ? 1 : 0, Math.round(n)) } as Partial<Widget>);
           }, 1, () => current?.[key] ?? 0)),
      );
    }
    host.append(el('fieldset', {}, el('legend', {}, 'Position and size'), geo));

    /* ---- data binding ---- */
    const binding: DataBinding = w.binding ?? { kind: 'owm-current', owmField: 'temp' };
    const bset = el('fieldset', {}, el('legend', {}, 'Data'));

    bset.append(
      field('Source', select(binding.kind, KINDS, (k) => {
        /* Changing the kind rebuilds the binding rather than patching it, so a stale
         * entityId cannot survive into an OWM binding and confuse the device. */
        const fresh: DataBinding = k === 'ha'
          ? { kind: 'ha', entityId: '' }
          : k === 'owm-daily'
            ? { kind: 'owm-daily', dayIndex: 0, owmField: 'max' }
            : k === 'owm-alert'
              ? { kind: 'owm-alert' }
              : { kind: 'owm-current', owmField: 'temp' };
        commit({ binding: fresh });
        render();
      })),
    );

    if (binding.kind === 'ha') {
      const list = document.createElement('datalist');
      list.id = 'ha-entities';
      for (const e of entities) {
        list.append(el('option', { value: e.entityId }, e.friendlyName ?? e.entityId));
      }
      const idc = el('input', {
        type: 'text', value: binding.entityId ?? '',
        placeholder: 'sensor.upstairs_hallway_temperature',
      }) as HTMLInputElement;
      /* Set the `list` ATTRIBUTE, not the property. The IDL property `input.list` is the
       * datalist ELEMENT, not its id, so assigning the id string there is a type error and
       * would not work at runtime either. */
      idc.setAttribute('list', 'ha-entities');
      idc.addEventListener('input', () => {
        commit({ binding: { kind: 'ha', entityId: idc.value } });
        opts.onValidateEntity?.(idc.value);
        /* A SEARCH PER KEYSTROKE, not one fetch at load. Home Assistant sends no CORS headers so
         * the browser cannot list entities itself, and the device-side search needs a term — so
         * the picker can only fill in as the user types. The shell debounces this. */
        opts.onEntitySearch?.(idc.value);
      });
      /* Held so setEntityOptions() can refill the dropdown WITHOUT re-rendering (which would
       * replace idc mid-typing and drop focus). */
      haDatalist = list;
      bset.append(field('Entity id', idc));
      /* The datalist must be in the document for the picker to work, and appended AFTER the
       * input that references it by id. */
      bset.append(list);
      /* A line the SEARCH can update in place — see setEntityOptions(). It reports how many
       * matches came back, or why the search failed, at the field the user is typing in, without
       * a re-render that would drop focus. */
      haNote = el('p', { className: 'hint' });
      haNote.textContent = 'Type part of the entity id — matching entities load from the display.';
      bset.append(haNote);
      /* Say WHY there is no dropdown when the fetch failed, rather than showing an empty one —
       * an empty picker reads as "your HA has no entities". */
      if (entitiesUnavailable) {
        bset.append(el('p', { className: 'hint warn' }, entitiesUnavailable));
      } else if (entities.length === 0) {
        bset.append(el('p', { className: 'hint' },
          'Type part of the entity id — matching entities load from the display as you type.'));
      }
    } else if (binding.kind === 'owm-daily') {
      bset.append(
        field('Day', select(String(binding.dayIndex ?? 0),
          [0, 1, 2, 3, 4].map((d) => ({ value: String(d), label: d === 0 ? 'Today' : `Day ${d + 1}` })),
          (v) => commit({ binding: { ...binding, dayIndex: Number(v) } }))),
        field('Value', select(binding.owmField ?? 'max', OWM_FIELDS,
          (v) => { commit({ binding: { ...binding, owmField: v } }); render(); })),
      );
    } else if (binding.kind === 'owm-current') {
      bset.append(
        field('Value', select(binding.owmField ?? 'temp', OWM_FIELDS,
          (v) => { commit({ binding: { ...binding, owmField: v } }); render(); })),
      );
      /* Say what the icon option will actually do. The editor's preview draws the picture, but a
       * user who has just switched to it deserves the sentence rather than having to infer it from
       * a preview that looks empty until a real icon code arrives. */
      if (binding.owmField === ICON_FIELD) {
        bset.append(el('p', { className: 'hint' },
          'This box draws the current weather icon as a PICTURE, not as text. Make it square so the '
          + 'icon is not distorted. It needs a rounded, square-ish box of at least 48 px to read.'));
      }
    }

    bset.append(el('p', { className: 'hint' }, describeBinding(binding)));
    host.append(bset);

    /* ---- formatting ---- */
    const fmt = w.format ?? {};
    const fset = el('fieldset', {}, el('legend', {}, 'Number format'));
    /* The "Shown as:" preview is UPDATED IN PLACE for the same reason as the alert description:
     * a re-render would replace the input mid-typing and drop focus. IT MUST READ THE WIDGET'S
     * CURRENT FORMAT, not the `fmt` captured above — commit() builds a NEW format object, so the
     * captured one still holds the old suffix and the hint would sit one edit behind. */
    const fmtHint = el('p', { className: 'hint' }, `Shown as: ${formatPlaceholder(fmt)}`);
    const refreshFmtHint = (): void => {
      fmtHint.textContent = `Shown as: ${formatPlaceholder(current?.format ?? {})}`;
    };
    fset.append(
      field('Decimals', numberInput(fmt.decimals ?? 1, (n) => { commit({ format: { ...fmt, decimals: n } }); refreshFmtHint(); }, 1)),
      field('Prefix', textInput(fmt.prefix ?? '', (s) => { commit({ format: { ...fmt, prefix: s } }); refreshFmtHint(); })),
      field('Suffix', textInput(fmt.suffix ?? '', (s) => { commit({ format: { ...fmt, suffix: s } }); refreshFmtHint(); })),
      field('When unavailable', textInput(fmt.fallback ?? '--', (s) => { commit({ format: { ...fmt, fallback: s } }); refreshFmtHint(); })),
      fmtHint,
    );
    host.append(fset);

    /* ---- font ---- */
    const font = w.font ?? { size: 64, align: 'left' as const, valign: 'top' as const };
    const tset = el('fieldset', {}, el('legend', {}, 'Text'));
    /* The faces the panel HAS, not a free number: the device cannot rasterise an arbitrary
     * size, so a spinner offering 10-200 would be a control that mostly does nothing. The list
     * is the generated LADDER, so it grows automatically when the ladder does and can never
     * name a size the device cannot draw. */
    tset.append(
      field('Size', select(String(sizeFor(w)), FACES_AVAILABLE.map((f) => ({ value: String(f.px), label: f.label })), (v) => {
        commit({ font: { ...font, size: Number(v) } });
        render();
      })),
    );
    tset.append(
      field('Align', select(font.align, [
        { value: 'left' as const, label: 'Left' },
        { value: 'center' as const, label: 'Centre' },
        { value: 'right' as const, label: 'Right' },
      ], (v) => commit({ font: { ...font, align: v } }))),
      field('Vertical', select(font.valign, [
        { value: 'top' as const, label: 'Top' },
        { value: 'middle' as const, label: 'Middle' },
        { value: 'bottom' as const, label: 'Bottom' },
      ], (v) => commit({ font: { ...font, valign: v } }))),
    );
    host.append(tset);

    /* ---- alert rules ---- */
    /* READ THE WIDGET'S RULES FRESH IN EVERY HANDLER, never a copy captured at render.
     *
     * commit() REPLACES w.alerts with a new array, so a captured reference goes stale the moment
     * any field is edited. The handlers used to re-render after each change, which rebuilt them
     * with the new array and hid this — but re-rendering also drops focus mid-typing, so it had
     * to go. Reading fresh is what makes editing two fields of one rule keep BOTH: with a stale
     * capture, setting the operator and then the threshold spread the ORIGINAL rule for the
     * second edit and silently reverted the operator. */
    const alertsOf = (): AlertRule[] => current?.alerts ?? [];
    const nAlert = alertsOf().length;
    const aset = el('fieldset', {}, el('legend', {}, 'Alerts'));
    for (let i = 0; i < nAlert; i++) {
      /* The description line is updated IN PLACE, for the same focus reason. */
      const desc = el('p', { className: 'hint' }, describeRule(alertsOf()[i]!));
      const edit = (patch: Partial<AlertRule>): void => {
        const next = alertsOf().slice();
        next[i] = { ...next[i]!, ...patch };
        commit({ alerts: next });
        desc.textContent = describeRule(next[i]!);
      };
      const row = el('div', { className: 'pRule' },
        select(alertsOf()[i]!.op, OPS.map((o) => ({ value: o, label: o })), (v) => edit({ op: v })),
        numberInput(alertsOf()[i]!.threshold, (n) => edit({ threshold: n })),
        select(alertsOf()[i]!.level, LEVELS.map((l) => ({ value: l, label: l })), (v) => edit({ level: v })),
        makeButton('×', () => {
          const next = alertsOf().slice();
          next.splice(i, 1);
          commit({ alerts: next });
          render();
        }),
      );
      aset.append(row, desc);
    }
    aset.append(makeButton('Add rule', () => {
      /* The device holds at most MAX_ALERT_RULES_PER_WIDGET rules per widget and ignores the
       * rest, so a rule added past the cap would be a control that appears to work while having
       * no effect on the glass. */
      const cur = alertsOf();
      if (cur.length >= MAX_ALERT_RULES_PER_WIDGET) return;
      commit({ alerts: [...cur, { op: 'gt', threshold: 100, level: 'severe' }] });
      render();
    }));
    if (nAlert >= MAX_ALERT_RULES_PER_WIDGET) {
      aset.append(el('p', { className: 'hint' },
        `The display uses at most ${MAX_ALERT_RULES_PER_WIDGET} alert rules per box.`));
    }
    host.append(aset);

    /* ---- delete ---- */
    host.append(el('div', { className: 'actions' },
      makeButton('Delete box', () => onDelete(currentSel!))));
  }

  render();

  return {
    show(sel, page) {
      currentSel = sel;
      currentPage = page;
      current = sel?.kind === 'widget' ? page.widgets.find((x) => x.id === sel.id) : undefined;
      render();
    },
    setEntities(list, unavailable) {
      entities = list;
      entitiesUnavailable = unavailable;
      render();
    },
    setEntityOptions(list, note, total) {
      /* NO render() — see the interface note. Only refill the dropdown that is already on screen,
       * so the input keeps focus and the typed text. A missing datalist (no HA widget selected)
       * makes this a no-op rather than an error: a search can land after the user clicked away. */
      entities = list;
      if (haDatalist) {
        haDatalist.replaceChildren();
        for (const e of list) {
          haDatalist.append(el('option', { value: e.entityId }, e.friendlyName ?? e.entityId));
        }
      }
      if (!haNote) return;
      if (note) {
        haNote.textContent = note;
        haNote.classList.add('warn');
      } else {
        const n = list.length;
        /* Say when the display matched MORE than it returned, rather than presenting a clipped
         * list as the whole set — the same "says a value it cannot know" class this picker had. */
        const more = total !== undefined && total > n;
        haNote.textContent = n === 0
          ? 'No entities matched. Check the spelling, or type the full id.'
          : `${n} matching ${n === 1 ? 'entity' : 'entities'}${more ? ` of ${total} — refine the search to narrow it` : ''} — pick one from the list.`;
        haNote.classList.remove('warn');
      }
    },
  };
}
