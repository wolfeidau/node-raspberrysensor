'use strict';

var raspberrysensor = require('../lib/raspberrysensor.js'),
    should = require('should');

describe('raspberrysensor', function () {
    it('should succeed', function (done) {
      raspberrysensor.async(function(err, result){
        should.not.exist(err);
        console.log(result);
        done();
      });
    });
});

