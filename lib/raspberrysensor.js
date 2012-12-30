/*
 * raspberrysensor
 * https://github.com/markw/raspberrysensor
 *
 * Copyright (c) 2012 Mark Wolfe
 * Licensed under the MIT license.
 */
'use strict';


var bindings = require('bindings')('raspberrysensor');

module.exports.humidity = function(callback) {
  return bindings.humidity(callback);
};
