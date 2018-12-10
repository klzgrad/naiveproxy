// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @ts-check
'use strict';

/**
 * @fileoverview
 * Methods for manipulating the state and the DOM of the page
 */

/** @type {HTMLFormElement} Form containing options and filters */
const form = document.getElementById('options');

/** Utilities for working with the DOM */
const dom = {
  /**
   * Create a document fragment from the given nodes
   * @param {Iterable<Node>} nodes
   * @returns {DocumentFragment}
   */
  createFragment(nodes) {
    const fragment = document.createDocumentFragment();
    for (const node of nodes) fragment.appendChild(node);
    return fragment;
  },
  /**
   * Removes all the existing children of `parent` and inserts
   * `newChild` in their place
   * @param {Node} parent
   * @param {Node | null} newChild
   */
  replace(parent, newChild) {
    while (parent.firstChild) parent.removeChild(parent.firstChild);
    if (newChild != null) parent.appendChild(newChild);
  },
  /**
   * Builds a text element in a single statement.
   * @param {string} tagName Type of the element, such as "span".
   * @param {string} text Text content for the element.
   * @param {string} [className] Class to apply to the element.
   */
  textElement(tagName, text, className) {
    const element = document.createElement(tagName);
    element.textContent = text;
    if (className) element.className = className;
    return element;
  },
};

/** Build utilities for working with the state. */
function _initState() {
  const _DEFAULT_FORM = new FormData(form);

  /**
   * State is represented in the query string and
   * can be manipulated by this object. Keys in the query match with
   * input names.
   */
  let _filterParams = new URLSearchParams(location.search.slice(1));
  const typeList = _filterParams.getAll(_TYPE_STATE_KEY);
  _filterParams.delete(_TYPE_STATE_KEY);
  for (const type of types(typeList)) {
    _filterParams.append(_TYPE_STATE_KEY, type);
  }

  const state = Object.freeze({
    /**
     * Returns a string from the current query string state.
     * @param {string} key
     * @returns {string | null}
     */
    get(key) {
      return _filterParams.get(key);
    },
    /**
     * Checks if a key is present in the query string state.
     * @param {string} key
     * @returns {boolean}
     */
    has(key) {
      return _filterParams.has(key);
    },
    /**
     * Formats the filter state as a string.
     */
    toString() {
      const copy = new URLSearchParams(_filterParams);
      const types = [...new Set(copy.getAll(_TYPE_STATE_KEY))];
      if (types.length > 0) copy.set(_TYPE_STATE_KEY, types.join(''));

      const queryString = copy.toString();
      return queryString.length > 0 ? `?${queryString}` : '';
    },
    /**
     * Saves a key and value into a temporary state not displayed in the URL.
     * @param {string} key
     * @param {string | null} value
     */
    set(key, value) {
      if (value == null) {
        _filterParams.delete(key);
      } else {
        _filterParams.set(key, value);
      }
      history.replaceState(null, null, state.toString());
    },
  });

  // Update form inputs to reflect the state from URL.
  for (const element of Array.from(form.elements)) {
    if (element.name) {
      const input = /** @type {HTMLInputElement} */ (element);
      const values = _filterParams.getAll(input.name);
      const [value] = values;
      if (value) {
        switch (input.type) {
          case 'checkbox':
            input.checked = values.includes(input.value);
            break;
          case 'radio':
            input.checked = value === input.value;
            break;
          default:
            input.value = value;
            break;
        }
      }
    }
  }

  /**
   * Yields only entries that have been modified in
   * comparison to `_DEFAULT_FORM`.
   * @param {FormData} modifiedForm
   */
  function* onlyChangedEntries(modifiedForm) {
    // Remove default values
    for (const key of modifiedForm.keys()) {
      const modifiedValues = modifiedForm.getAll(key);
      const defaultValues = _DEFAULT_FORM.getAll(key);

      const valuesChanged =
        modifiedValues.length !== defaultValues.length ||
        modifiedValues.some((v, i) => v !== defaultValues[i]);
      if (valuesChanged) {
        for (const value of modifiedValues) {
          yield [key, value];
        }
      }
    }
  }

  // Update the state when the form changes.
  function _updateStateFromForm() {
    const modifiedForm = new FormData(form);
    _filterParams = new URLSearchParams(onlyChangedEntries(modifiedForm));
    history.replaceState(null, null, state.toString());
  }

  form.addEventListener('change', _updateStateFromForm);

  return state;
}

function _startListeners() {
  const _SHOW_OPTIONS_STORAGE_KEY = 'show-options';

  /** @type {HTMLFieldSetElement} */
  const typesFilterContainer = document.getElementById('types-filter');
  /** @type {HTMLInputElement} */
  const methodCountInput = form.elements.namedItem('method_count');
  /** @type {HTMLFieldSetElement} */
  const byteunit = form.elements.namedItem('byteunit');
  /** @type {HTMLCollectionOf<HTMLInputElement>} */
  const typeCheckboxes = form.elements.namedItem(_TYPE_STATE_KEY);
  /** @type {HTMLSpanElement} */
  const sizeHeader = document.getElementById('size-header');

  /**
   * The settings dialog on the side can be toggled on and off by elements with
   * a 'toggle-options' class.
   */
  function _toggleOptions() {
    const openedOptions = document.body.classList.toggle('show-options');
    localStorage.setItem(_SHOW_OPTIONS_STORAGE_KEY, openedOptions.toString());
  }
  for (const button of document.getElementsByClassName('toggle-options')) {
    button.addEventListener('click', _toggleOptions);
  }
  // Default to open if getItem returns null
  if (localStorage.getItem(_SHOW_OPTIONS_STORAGE_KEY) !== 'false') {
    document.body.classList.add('show-options');
  }

  /**
   * Disable some fields when method_count is set
   */
  function setMethodCountModeUI() {
    if (methodCountInput.checked) {
      byteunit.setAttribute('disabled', '');
      typesFilterContainer.setAttribute('disabled', '');
      sizeHeader.textContent = 'Methods';
    } else {
      byteunit.removeAttribute('disabled');
      typesFilterContainer.removeAttribute('disabled');
      sizeHeader.textContent = 'Size';
    }
  }
  setMethodCountModeUI();
  methodCountInput.addEventListener('change', setMethodCountModeUI);

  /**
   * Display error text on blur for regex inputs, if the input is invalid.
   * @param {Event} event
   */
  function checkForRegExError(event) {
    const input = /** @type {HTMLInputElement} */ (event.currentTarget);
    const errorBox = document.getElementById(
      input.getAttribute('aria-describedby')
    );
    try {
      new RegExp(input.value);
      errorBox.textContent = '';
      input.setAttribute('aria-invalid', 'false');
    } catch (err) {
      errorBox.textContent = err.message;
      input.setAttribute('aria-invalid', 'true');
    }
  }
  for (const input of document.getElementsByClassName('input-regex')) {
    input.addEventListener('blur', checkForRegExError);
    input.dispatchEvent(new Event('blur'));
  }

  document.getElementById('type-all').addEventListener('click', () => {
    for (const checkbox of typeCheckboxes) {
      checkbox.checked = true;
    }
    form.dispatchEvent(new Event('change'));
  });
  document.getElementById('type-none').addEventListener('click', () => {
    for (const checkbox of typeCheckboxes) {
      checkbox.checked = false;
    }
    form.dispatchEvent(new Event('change'));
  });
}

function _makeIconTemplateGetter() {
  const _icons = document.getElementById('icons');

  /**
   * @type {{[type:string]: SVGSVGElement}} Icon elements
   * that correspond to each symbol type.
   */
  const symbolIcons = {
    D: _icons.querySelector('.foldericon'),
    C: _icons.querySelector('.componenticon'),
    J: _icons.querySelector('.javaclassicon'),
    F: _icons.querySelector('.fileicon'),
    b: _icons.querySelector('.bssicon'),
    d: _icons.querySelector('.dataicon'),
    r: _icons.querySelector('.readonlyicon'),
    t: _icons.querySelector('.codeicon'),
    R: _icons.querySelector('.relroicon'),
    '*': _icons.querySelector('.generatedicon'),
    x: _icons.querySelector('.dexicon'),
    m: _icons.querySelector('.dexmethodicon'),
    p: _icons.querySelector('.localpakicon'),
    P: _icons.querySelector('.nonlocalpakicon'),
    o: _icons.querySelector('.othericon'), // used as default icon
  };

  /** @type {Map<string, {color:string,description:string}>} */
  const iconInfoCache = new Map();

  /**
   * Returns the SVG icon template element corresponding to the given type.
   * @param {string} type Symbol type character.
   * @param {boolean} readonly If true, the original template is returned.
   * If false, a copy is returned that can be modified.
   * @returns {SVGSVGElement}
   */
  function getIconTemplate(type, readonly = false) {
    const iconTemplate = symbolIcons[type] || symbolIcons[_OTHER_SYMBOL_TYPE];
    return readonly ? iconTemplate : iconTemplate.cloneNode(true);
  }

  /**
   * Returns style info about SVG icon template element corresponding to the
   * given type.
   * @param {string} type Symbol type character.
   */
  function getIconStyle(type) {
    let info = iconInfoCache.get(type);
    if (info == null) {
      const icon = getIconTemplate(type, true);
      info = {
        color: icon.getAttribute('fill'),
        description: icon.querySelector('title').textContent,
      };
      iconInfoCache.set(type, info);
    }
    return info;
  }

  return {getIconTemplate, getIconStyle};
}

function _makeSizeTextGetter() {
  const _SIZE_CHANGE_CUTOFF = 50000;

  /**
   * Create the contents for the size element of a tree node.
   * The unit to use is selected from the current state.
   *
   * If in method count mode, size instead represents the amount of methods in
   * the node. Otherwise, the original number of bytes will be displayed.
   *
   * @param {TreeNode} node Node whose size is the number of bytes to use for
   * the size text
   * @returns {GetSizeResult} Object with hover text title and
   * size element body. Can be consumed by `_applySizeFunc()`
   */
  function getSizeContents(node) {
    if (state.has('method_count')) {
      const {count: methodCount = 0} =
        node.childStats[_DEX_METHOD_SYMBOL_TYPE] || {};
      const methodStr = methodCount.toLocaleString(_LOCALE, {
        useGrouping: true,
      });

      return {
        element: document.createTextNode(methodStr),
        description: `${methodStr} method${methodCount === 1 ? '' : 's'}`,
        value: methodCount,
      };
    } else {
      const bytes = node.size;
      let unit = state.get('byteunit');
      let suffix = _BYTE_UNITS[unit];
      if (suffix == null) {
        unit = 'MiB';
        suffix = _BYTE_UNITS.MiB;
      }

      // Format the bytes as a number with 2 digits after the decimal point
      const text = (bytes / suffix).toLocaleString(_LOCALE, {
        minimumFractionDigits: 2,
        maximumFractionDigits: 2,
      });
      const textNode = document.createTextNode(`${text} `);

      // Display the suffix with a smaller font
      const suffixElement = dom.textElement('small', unit);

      const bytesGrouped = bytes.toLocaleString(_LOCALE, {useGrouping: true});

      return {
        element: dom.createFragment([textNode, suffixElement]),
        description: `${bytesGrouped} bytes`,
        value: bytes,
      };
    }
  }

  /**
   * Set classes on an element based on the size it represents.
   * @param {HTMLElement} sizeElement
   * @param {number} value
   */
  function setSizeClasses(sizeElement, value) {
    const shouldHaveStyle =
      state.has('diff_mode') && Math.abs(value) > _SIZE_CHANGE_CUTOFF;
    if (shouldHaveStyle) {
      if (value < 0) {
        sizeElement.classList.add('shrunk');
        sizeElement.classList.remove('grew');
      } else {
        sizeElement.classList.remove('shrunk');
        sizeElement.classList.add('grew');
      }
    } else {
      sizeElement.classList.remove('shrunk', 'grew');
    }
  }

  return {getSizeContents, setSizeClasses};
}

/** Utilities for working with the state */
const state = _initState();
const {getIconTemplate, getIconStyle} = _makeIconTemplateGetter();
const {getSizeContents, setSizeClasses} = _makeSizeTextGetter();
_startListeners();
