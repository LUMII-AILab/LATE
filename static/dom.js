
export function select(selectorOrElement) {
  if (typeof selectorOrElement === 'string') {
    return document.querySelector(selectorOrElement);
  }
  return selectorOrElement
}

export function enable(el) {
  select(el).classList.remove('disabled');
}

export function disable(el) {
  select(el).classList.add('disabled');
}

export function hide(el) {
  select(el).classList.add('hidden');
}

export function unhide(el) {
  select(el).classList.remove('hidden');
}

export function on(el, event, listener) {
  return select(el)?.addEventListener(event, listener);
}

export function onclick(el, listener) {
  return on(el, 'click', listener);
}

export function oninput(el, listener) {
  return on(el, 'input', listener);
}

export function onchange(el, listener) {
  return on(el, 'change', listener);
}

export function addclass(el, ...classes) {
  return select(el).classList.add(...classes);
}

export function rmclass(el, ...classes) {
  return select(el).classList.remove(...classes);
}

export function hasclass(el, cls) {
  return select(el).classList.contains(cls);
}
