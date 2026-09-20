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

import type { AlertLevel, AlertOp, DataBinding, DataSourceKind, Widget } from '../model/config';
import { describeRule } from '../alerts/rules';
import { describeBinding } from '../data/binding';
import { formatPlaceholder } from '../data/format';
import { FACES_AVAILABLE, sizeFor } from '../canvas/face';

export interface PropertyPanelOptions {
  host: HTMLElement;
  /** Called after any edit, with the widget that changed. */
  onChange: (w: Widget) => void;
  /** Entity ids to offer in the picker, if a list has been fetched. Empty is fine. */
  entities?: { entityId: string; friendlyName?: string }[];
  /** Reports what an entity id looks like, so a typo is caught as it is typed. */
  onValidateEntity?: (id: string) => void;
  /** True when the HA entity list could not be fetched, so offer free text instead. */
  entitiesUnavailable?: string;
}

export interface PropertyPanelHandle {
  /** Re-render for a different widget, or for no selection. */
  show(w: Widget | undefined): void;
  /** Replace the entity list after an async fetch completes. */
  setEntities(list: { entityId: string; friendlyName?: string }[], unavailable?: string): void;
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

const OWM_FIELDS = ['temp', 'min', 'max', 'wind', 'humidity', 'condition'] as const;
const KINDS: { value: DataSourceKind; label: string }[] = [
  { value: 'owm-current', label: 'Current weather' },
  { value: 'owm-daily', label: 'Forecast' },
  { value: 'owm-alert', label: 'Weather alerts' },
  { value: 'ha', label: 'Home Assistant entity' },
];
const OPS: AlertOp[] = ['gt', 'gte', 'lt', 'lte', 'eq', 'ne'];
const LEVELS: Exclude<AlertLevel, 'none'>[] = ['advisory', 'warning', 'severe'];

export function createPropertyPanel(opts: PropertyPanelOptions): PropertyPanelHandle {
  const { host, onChange } = opts;
  let entities = opts.entities ?? [];
  let entitiesUnavailable = opts.entitiesUnavailable;
  let current: Widget | undefined;

  /** Assign then notify — see the note at the top of the file. */
  function commit(patch: Partial<Widget>): void {
    if (!current) return;
    Object.assign(current, patch);
    onChange(current);
  }

  function field(label: string, control: HTMLElement): HTMLElement {
    const id = `p-${Math.random().toString(36).slice(2, 8)}`;
    control.id = id;
    return el('div', { className: 'pRow' }, el('label', { htmlFor: id }, label), control);
  }

  function numberInput(
    value: number,
    onInput: (n: number) => void,
    step = 1,
  ): HTMLInputElement {
    const i = el('input', { type: 'number', value: String(value), step: String(step) }) as HTMLInputElement;
    i.addEventListener('input', () => {
      const n = Number(i.value);
      /* A blank or half-typed number must not be written as NaN: the field is mid-edit and
       * NaN would propagate into the geometry and the config. */
      if (Number.isFinite(n)) onInput(n);
    });
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

    if (!current) {
      host.append(el('p', { className: 'hint' }, 'Select a value box on the panel to edit it.'));
      return;
    }
    const w = current;

    host.append(el('h3', {}, `Box: ${w.id}`));

    /* ---- geometry (numeric entry, in addition to dragging) ---- */
    const geo = el('div', { className: 'pGrid' });
    for (const [label, key] of [['X', 'x'], ['Y', 'y'], ['W', 'w'], ['H', 'h']] as const) {
      geo.append(
        el('div', {},
           el('label', {}, label),
           numberInput(w[key], (n) => {
             /* Geometry is clamped on the device side too, but clamping here keeps the
              * number the user sees equal to the number stored. */
             commit({ [key]: Math.max(key === 'w' || key === 'h' ? 1 : 0, Math.round(n)) } as Partial<Widget>);
             render();
           })),
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
      });
      bset.append(field('Entity id', idc));
      /* The datalist must be in the document for the picker to work, and appended AFTER the
       * input that references it by id. */
      bset.append(list);
      /* Say WHY there is no dropdown when the fetch failed, rather than showing an empty one —
       * an empty picker reads as "your HA has no entities". */
      if (entitiesUnavailable) {
        bset.append(el('p', { className: 'hint warn' }, entitiesUnavailable));
      } else if (entities.length === 0) {
        bset.append(el('p', { className: 'hint' }, 'Type the entity id; the list will load if Home Assistant is reachable.'));
      }
    } else if (binding.kind === 'owm-daily') {
      bset.append(
        field('Day', select(String(binding.dayIndex ?? 0),
          [0, 1, 2, 3, 4].map((d) => ({ value: String(d), label: d === 0 ? 'Today' : `Day ${d + 1}` })),
          (v) => commit({ binding: { ...binding, dayIndex: Number(v) } }))),
        field('Value', select(binding.owmField ?? 'max',
          OWM_FIELDS.map((f) => ({ value: f, label: f })),
          (v) => commit({ binding: { ...binding, owmField: v as DataBinding['owmField'] } }))),
      );
    } else if (binding.kind === 'owm-current') {
      bset.append(
        field('Value', select(binding.owmField ?? 'temp',
          OWM_FIELDS.map((f) => ({ value: f, label: f })),
          (v) => commit({ binding: { ...binding, owmField: v as DataBinding['owmField'] } }))),
      );
    }

    bset.append(el('p', { className: 'hint' }, describeBinding(binding)));
    host.append(bset);

    /* ---- formatting ---- */
    const fmt = w.format ?? {};
    const fset = el('fieldset', {}, el('legend', {}, 'Number format'));
    fset.append(
      field('Decimals', numberInput(fmt.decimals ?? 1, (n) => commit({ format: { ...fmt, decimals: n } }), 1)),
      field('Prefix', textInput(fmt.prefix ?? '', (s) => commit({ format: { ...fmt, prefix: s } }))),
      field('Suffix', textInput(fmt.suffix ?? '', (s) => commit({ format: { ...fmt, suffix: s } }))),
      field('When unavailable', textInput(fmt.fallback ?? '--', (s) => commit({ format: { ...fmt, fallback: s } }))),
      el('p', { className: 'hint' }, `Shown as: ${formatPlaceholder(fmt)}`),
    );
    host.append(fset);

    /* ---- font ---- */
    const font = w.font ?? { size: 64, align: 'left' as const, valign: 'top' as const };
    const tset = el('fieldset', {}, el('legend', {}, 'Text'));
    /* The two faces the panel HAS, not a free number: the device cannot rasterise an arbitrary
     * size, so a spinner offering 10-200 would be a control that mostly does nothing. */
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
    const rules = w.alerts ?? [];
    const aset = el('fieldset', {}, el('legend', {}, 'Alerts'));
    for (let i = 0; i < rules.length; i++) {
      const r = rules[i]!;
      const row = el('div', { className: 'pRule' },
        select(r.op, OPS.map((o) => ({ value: o, label: o })), (v) => {
          const next = rules.slice();
          next[i] = { ...r, op: v };
          commit({ alerts: next });
          render();
        }),
        numberInput(r.threshold, (n) => {
          const next = rules.slice();
          next[i] = { ...r, threshold: n };
          commit({ alerts: next });
          render();
        }),
        select(r.level, LEVELS.map((l) => ({ value: l, label: l })), (v) => {
          const next = rules.slice();
          next[i] = { ...r, level: v };
          commit({ alerts: next });
          render();
        }),
        makeButton('×', () => {
          const next = rules.slice();
          next.splice(i, 1);
          commit({ alerts: next });
          render();
        }),
      );
      aset.append(row, el('p', { className: 'hint' }, describeRule(r)));
    }
    aset.append(makeButton('Add rule', () => {
      commit({ alerts: [...rules, { op: 'gt', threshold: 100, level: 'severe' }] });
      render();
    }));
    host.append(aset);
  }

  render();

  return {
    show(w: Widget | undefined) {
      current = w;
      render();
    },
    setEntities(list, unavailable) {
      entities = list;
      entitiesUnavailable = unavailable;
      render();
    },
  };
}
