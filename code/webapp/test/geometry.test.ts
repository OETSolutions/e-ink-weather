import { describe, it, expect } from 'vitest';
import { hitTest, applyResize, clampToPanel, snap } from '../src/canvas/geometry';
import type { Widget } from '../src/model/config';
const w = (o: Partial<Widget> = {}): Widget => ({ id:'a', x:100, y:100, w:200, h:100, role:'dynamic', ...o });
describe('hit testing', () => {
  it('finds widget under cursor', () => {
    expect(hitTest([w()],200,150)?.id).toBe('a');
    expect(hitTest([w()],50,50)).toBeNull();
  });
  it('reports edge and corner zones', () => {
    expect(hitTest([w()],100,150)?.zone).toBe('w');
    expect(hitTest([w()],100,100)?.zone).toBe('nw');
    expect(hitTest([w()],200,150)?.zone).toBe('move');
  });
  it('prefers topmost', () => {
    const a=w({id:'a'}), b=w({id:'b',x:150,y:120});
    expect(hitTest([a,b],200,150)?.id).toBe('b');
  });
});
describe('resize', () => {
  it('east changes width not x', () => {
    const r=applyResize(w(),'e',{x:300,y:0},50,0,{grid:1,minW:20,minH:20});
    expect(r.x).toBe(100); expect(r.w).toBe(250);
  });
  it('west moves x and shrinks w together', () => {
    const r=applyResize(w(),'w',{x:0,y:0},20,0,{grid:1,minW:20,minH:20});
    expect(r.x).toBe(120); expect(r.w).toBe(180);
  });
  it('never below minimum', () => {
    const r=applyResize(w(),'e',{x:0,y:0},-1000,0,{grid:1,minW:20,minH:20});
    expect(r.w).toBe(20);
  });
});
describe('clamp and snap', () => {
  it('pulls widget inside panel', () => {
    const r=clampToPanel({x:900,y:660,w:200,h:100});
    expect(r.x+r.w).toBeLessThanOrEqual(920);
    expect(r.y+r.h).toBeLessThanOrEqual(680);
  });
  it('snaps to grid', () => {
    expect(snap(103,8,[])).toBe(104);
    expect(snap(99,8,[])).toBe(96);
  });
  it('prefers guide over grid', () => {
    expect(snap(101,8,[100])).toBe(100);
  });
});
