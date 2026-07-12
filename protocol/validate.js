// Buildless JSON-Schema (subset) validator for the OBD1 Scanner protocol.
//
// Canonical shapes live in protocol/schema.json; this file never duplicates
// them, it loads and enforces them. No dependencies, no build step - works as
// an ES module in the browser (`fetch`) or with a schema object passed in
// (Node tests). Supports the keyword subset the schema actually uses:
//   type, required, properties, items, enum, minimum, additionalProperties,
//   and $ref of the form "#/$defs/Name".
//
// Usage (browser / transport.js):
//   import { validateSnapshot } from '../protocol/validate.js';
//   const errs = await validateSnapshot(snapshot);   // [] when valid
//
// Usage (Node test): pass the parsed schema.json in via { schema }.

let _schemaCache = null;

export async function loadSchema(url = new URL('./schema.json', import.meta.url)) {
  if (!_schemaCache) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`schema load failed: ${res.status} ${url}`);
    _schemaCache = await res.json();
  }
  return _schemaCache;
}

// Let callers (Node tests) inject a schema object instead of fetching.
export function setSchema(schema) { _schemaCache = schema; return schema; }

function typeOf(v) {
  if (v === null) return 'null';
  if (Array.isArray(v)) return 'array';
  if (typeof v === 'number' && Number.isInteger(v)) return 'integer';
  return typeof v; // 'number' | 'string' | 'boolean' | 'object'
}

function typeMatches(expected, v) {
  const t = typeOf(v);
  const list = Array.isArray(expected) ? expected : [expected];
  return list.some(e =>
    e === t ||
    (e === 'number' && t === 'integer'));   // an integer is a valid number
}

export function makeValidator(schema) {
  const defs = schema.$defs || schema.definitions || {};

  function resolve(node) {
    if (node && node.$ref) {
      const m = /^#\/\$defs\/(.+)$/.exec(node.$ref);
      if (!m || !defs[m[1]]) throw new Error(`unresolvable $ref: ${node.$ref}`);
      return defs[m[1]];
    }
    return node;
  }

  function check(node, v, path, errs) {
    node = resolve(node);

    if (node.type && !typeMatches(node.type, v)) {
      errs.push(`${path}: expected ${node.type}, got ${typeOf(v)}`);
      return; // wrong type - deeper checks would be noise
    }
    if (node.enum && !node.enum.includes(v)) {
      errs.push(`${path}: ${JSON.stringify(v)} not one of ${JSON.stringify(node.enum)}`);
    }
    if (typeof node.minimum === 'number' && typeof v === 'number' && v < node.minimum) {
      errs.push(`${path}: ${v} below minimum ${node.minimum}`);
    }

    if (typeOf(v) === 'object' && (node.properties || node.required || node.additionalProperties !== undefined)) {
      for (const req of node.required || []) {
        if (!(req in v)) errs.push(`${path}: missing required "${req}"`);
      }
      for (const [k, val] of Object.entries(v)) {
        if (node.properties && node.properties[k]) {
          check(node.properties[k], val, `${path}.${k}`, errs);
        } else if (node.additionalProperties === false) {
          errs.push(`${path}: unexpected property "${k}"`);
        } else if (node.additionalProperties && typeof node.additionalProperties === 'object') {
          check(node.additionalProperties, val, `${path}.${k}`, errs);
        }
        // else: additive properties allowed (forward-compatible by default)
      }
    }

    if (node.items && Array.isArray(v)) {
      v.forEach((item, i) => check(node.items, item, `${path}[${i}]`, errs));
    }
  }

  // Returns an array of error strings; empty means valid.
  return function validate(instance, defName) {
    const root = defName ? { $ref: `#/$defs/${defName}` } : schema;
    const errs = [];
    check(root, instance, defName || '$', errs);
    return errs;
  };
}

async function _validate(instance, defName, opts) {
  const schema = opts.schema ? setSchema(opts.schema) : await loadSchema(opts.url);
  const errs = makeValidator(schema)(instance, defName);
  if (errs.length && (opts.throwOnError ?? false)) {
    throw new Error(`${defName} schema violation:\n  ${errs.join('\n  ')}`);
  }
  return errs;
}

export const validateSnapshot   = (o, opts = {}) => _validate(o, 'Snapshot', opts);
export const validateParams     = (o, opts = {}) => _validate(o, 'ParamsResponse', opts);
export const validateSubscribe  = (o, opts = {}) => _validate(o, 'SubscribeCommand', opts);
export const validateAck        = (o, opts = {}) => _validate(o, 'SubscribeAck', opts);
