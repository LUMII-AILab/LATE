// This script can be used to dynamically load ESM module importmap from url or javascript object.
// Unfortunately because of limitations, few changes to the source may be needed for this to work.
//
// The intended use of this is during development phase possibly avoiding the need for complex
// build systems and relying solely on browser native solutions for loading ESM modules.
//
// Usage:
//
// 1. set type of all ESM module script tags to 'module-deferred', i.e., like this
//
// <script type="module-deferred">
//  ... ESM module content ...
// </script>
//
//
// 2. import this script as non-ESM module, i.e., do not set type="module" for it, i.e.,
// <script src="bootstrap.js"></script>
//
// 3. call bootstrap()
//
// <script>
//    bootstrap();  // loads importmap.json and executes all script tags with type 'module-deferred'
// </script>
//
//
// there are multiple argument options available for bootstrap(...) call:
//
// * using defaults:
//    bootstrap()
//
//   - without any arguments the default importmap.json will be loaded as importmap,
//     there are multiple options available to override this:
//     - specify global variable defaultImportMap (i.e., set window.defaultImportMap to either path
//       of the import map json file to be loaded from server or importmap itself as an object);
//     - using options object as described below;
//
// * loading one or more external scripts by urls:
//    bootstrap(...[list of script urls])
//
// * passing options object with one or more fields to override defaults and load external scripts:
//    bootstrap({
//      importmap: <path to importmap.json or importmap as object directly>,
//      override: { ... importmap object to override importmap setting field by field ... },
//      scripts: [... list of script url's to load as ESM modules ...]
//    })
//
//   - the fields of the options object are optional;
//   - the second importmap under override key will override the importmap specified under importmap key;
//   - only fields present under override object will be overridden;
//
// * combination of previous two cases:
//    bootstrap({ ... options object as above ... }, ...[list of script urls])
//

var injectingImportMap;
const defaultImportMapLocation = 'importmap.json';

function injectImportMap(map) {
  const script = document.createElement('script');
  script.type = 'importmap';
  script.textContent = typeof map === 'string' ? map : JSON.stringify(map);
  document.head.appendChild(script);
}

function mergeImportMaps(baseMap, overrideMap) {
  if (!baseMap && !overrideMap) {
    return;
  }
  if (!baseMap && overrideMap) {
    return overrideMap;
  }
  if (baseMap && !overrideMap) {
    return baseMap;
  }
  if (baseMap.imports && overrideMap.imports) {
    Object.assign(baseMap.imports, overrideMap.imports);
  }
  if (baseMap.imports && overrideMap.imports) {
    Object.assign(baseMap.imports, overrideMap.imports);
  } else if (overrideMap.imports) {
    baseMap.imports = overrideMap.imports;
  }
  if (baseMap.integrity && overrideMap.integrity) {
    Object.assign(baseMap.integrity, overrideMap.integrity);
  } else if (overrideMap.integrity) {
    baseMap.integrity = overrideMap.integrity;
  }
  if (baseMap.scopes && overrideMap.scopes) {
    Object.assign(baseMap.scopes, overrideMap.scopes);
  } else if (overrideMap.scopes) {
    baseMap.scopes = overrideMap.scopes;
  }
  return baseMap;
}

function fetchAndInjectImportMap(importMap, overrideMap = undefined) {
  // console.debug(`fetching importmap ${url}`);
  injectingImportMap = new Promise(async (resolve, reject) => {
    try {
      if (typeof importMap === 'string') {
        const response = await fetch(importMap);
        if (!response.ok) {
          console.error(`unable to load importmap from ${importMap}, got response status ${response.status}`);
          resolve(false);
          return;
        }
        importMap = await response.json();
      }
      injectImportMap(mergeImportMaps(importMap, overrideMap));
      resolve(true);
    } catch (error) {
      injectingImportMap = undefined;
      console.error('Error loading import map:', error);
      // reject(error);
      resolve(false);
    }
  });
  return injectingImportMap;
}

function addModuleScript(src) {
  const script = document.createElement('script');
  script.src = src;
  script.type = 'module';
  document.body.appendChild(script);
}

async function bootstrap(...entrypointScripts) {
  let importMap = window.defaultImportMap || defaultImportMapLocation;
  let overrideMap;
  if (entrypointScripts.length >= 1) {
    const firstItem = entrypointScripts[0];
    if (typeof firstItem === 'object') {
      if (firstItem.importmap || firstItem.override || firstItem.scripts) {
        const options = entrypointScripts.shift();
        if (options.scripts) {
          entrypointScripts = entrypointScripts.concat(options.scripts);
        }
        if (options.importmap) {
          importMap = options.importmap;
        }
        if (options.override) {
          overrideMap = options.override;
        }
      } else if (firstItem.imports) {
        importMap = entrypointScripts.shift();
      }
    }
  }
  // document.addEventListener('DOMContentLoaded', () => { });  // NOTE: all script tags may be available only after DOM is loaded
  const importMapPresent = document.querySelector('script[type="importmap"]') != null;
  if (importMapPresent || (injectingImportMap != undefined && await injectingImportMap) || await fetchAndInjectImportMap(importMap, overrideMap)) {
    for (const entrypointScript of entrypointScripts) {
      addModuleScript(entrypointScript);
    }
    const scripts = document.querySelectorAll('script[type="module-deferred"]');
    for (const script of scripts) {
      const newScript = document.createElement('script');
      newScript.type = 'module';
      if (script.src && script.src.length > 0) {
        newScript.src = script.src;
      }
      newScript.textContent = script.textContent;
      script.remove();                      // remove the old script
      document.body.appendChild(newScript); // re-append new script to trigger execution
    }
  }
}
