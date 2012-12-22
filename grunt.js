'use strict';

module.exports = function (grunt) {

    // Project configuration.
    grunt.initConfig({
        pkg:'<json:package.json>',
        test:{
            files:['test/**/*.js']
        },
        lint:{
            files:['grunt.js', 'lib/**/*.js', 'test/**/*.js']
        },
        watch:{
            files:'<config:lint.files>',
            tasks:'default'
        },
        jshint:{
            options:{
                curly:true,
                eqeqeq:true,
                immed:true,
                latedef:true,
                newcap:true,
                noarg:true,
                sub:true,
                undef:true,
                boss:true,
                eqnull:true,
                node:true,
                "predef":[
                    "describe", // Used by mocha
                    "it", // Used by mocha
                    "before", // Used by mocha
                    "beforeEach", // Used by mocha
                    "after", // Used by mocha
                    "afterEach"      // Used by mocha
                ]
            },
            globals:{
                exports:true
            }
        },
        simplemocha:{
            all: {
                src: 'test/**/*.js',
                options: {
                    globals: ['should'],
                    timeout: 3000,
                    ignoreLeaks: false,
                    grep: '*-test',
                    ui: 'bdd',
                    reporter: 'spec'
                }
            }
        }
    });

    grunt.loadNpmTasks('grunt-simple-mocha');

    // Default task.
    grunt.registerTask('default', ['lint', 'simplemocha']);

    // override the default test target
    grunt.registerTask('test', 'simplemocha');

};