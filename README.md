# raspberrysensor

Library for accessing various sensors via the bcm2835 library.

The first sensor which I have implemented is the [AM2302](http://cdn.shopify.com/s/files/1/0045/8932/files/DHT22.pdf?100745), to see how this is attached to the Raspberry Pi take a look
at my blog post [Raspberry Pi Temperature and Humidity Project Construction](www.wolfe.id.au/2012/12/22/raspberry-pi-temperature-and-humidity-project-construction/)

Once you have your sensor assembled and attached to the Raspberry Pi you can install node and try it out.

## Getting Started

As this is a native node.js module you will first need to setup your pi with the necessary tools.

Firstly install build essential and git.

```
sudo apt-get build-essential
```

Then download node and build it.

```
wget https://github.com/joyent/node/archive/v0.8.16.tar.gz
tar cvzf v0.8.16.tar.gz
cd node-0.8.16
./configure
sudo make install
```

Download and install the [bcm2835 library](http://www.open.com.au/mikem/bcm2835/).

```
wget http://www.open.com.au/mikem/bcm2835/bcm2835-1.14.tar.gz
cd bcm2835-1.14
./configure
sudo make install
```

Now Install the module with: `npm install raspberrysensor`

```javascript
var raspberrysensor = require('raspberrysensor');

raspberrysensor.humidity(function(err, result){
  console.log(JSON.stringify(result));
});
```

This will output something like.

```javascript
{"humidity_integral":52,"humidity_decimal":1,"temperature_integral":23,"temperature_decimal":7}
```

## Contributing
In lieu of a formal styleguide, take care to maintain the existing coding style. Add unit tests for any new or changed functionality. Lint and test your code using [grunt](https://github.com/gruntjs/grunt).

## Release History

0.1.0 Initial release.

0.1.1 Fixed documentation.

## License
Copyright (c) 2012 Mark Wolfe  
Licensed under the MIT license.
