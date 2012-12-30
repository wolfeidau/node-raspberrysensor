/*
 * raspberrysensor
 * https://github.com/markw/raspberrysensor
 *
 * Access an AM2302 temperature and humidity sensor using the GPIO ports on the Raspberry Pi
 *
 * Copyright (c) 2012 Mark Wolfe
 * Licensed under the MIT license.
 *
 * Started with information, insight and code linked in this blog post.
 *  http://blog.ringerc.id.au/2012/01/using-rht03-aliases-rht-22.html
 *
 * Used bits of this code by Miguel Moreto, Brazil, 2012. 
 *  https://code.google.com/p/moreto-nixie-clock/source/browse/trunk/FirmwareV1/DHT22.c
 * 
 * Pretty much rewrote it line by line while working out how the sensor and some thier code worked then tweaked it for this platform.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/io.h>
#include <bcm2835.h>

#define DHT22_DATA_BIT_COUNT 40
//#define DEBUG 1

#include <node.h>
#include <string>

using namespace v8;

static inline int timespec_cmp (struct timespec a, struct timespec b)
{
  return (a.tv_sec < b.tv_sec ? -1
    : a.tv_sec > b.tv_sec ? 1
    : a.tv_nsec - b.tv_nsec);
}

// Forward declaration. Usually, you do this in a header file.
Handle<Value> Humidity(const Arguments& args);
void HumidityWork(uv_work_t* req);
void HumidityAfter(uv_work_t* req);

// We use a struct to store information about the asynchronous "work request".
struct Baton {
  // This handle holds the callback function we'll call after the work request
  // has been completed in a threadpool thread. It's persistent so that V8
  // doesn't garbage collect it away while our request waits to be processed.
  // This means that we'll have to dispose of it later ourselves.
  Persistent<Function> callback;

  // The GPIO pin which will be read
  int32_t pin;

  // return values 
  int32_t humidity_integral;
  int32_t humidity_decimal;
  int32_t temperature_integral;
  int32_t temperature_decimal;

  // time taken
  int32_t time_taken;

  // time stamps
  struct timespec t_before;
  struct timespec t_after;
 
  // Tracking errors that happened in the worker function. You can use any
  // variables you want. E.g. in some cases, it might be useful to report
  // an error number.
  bool error;

  // an optional error message
  std::string error_message;

  // Custom data you can pass through.
  int32_t result;
};

// This is the function called directly from JavaScript land. It creates a
// work request object and schedules it for execution.
Handle<Value> Humidity(const Arguments& args) {
  HandleScope scope;

  if (!args[0]->IsFunction()) {
    return ThrowException(Exception::TypeError(
          String::New("First argument must be a callback function.")));
  }

  if (!args[1]->IsUndefined() && !args[1]->IsNumber()) {
    return ThrowException(Exception::TypeError(
          String::New("Second argument must be the number of the pin to read data from.")));
  }

  // There's no ToFunction(), use a Cast instead.
  Local<Function> callback = Local<Function>::Cast(args[0]);

  // The baton holds our custom status information for this asynchronous call,
  // like the callback function we want to call when returning to the main
  // thread and the status information.
  Baton* baton = new Baton();
  baton->error = false;
  baton->callback = Persistent<Function>::New(callback);
  baton->pin = args[1]->IsUndefined() ? 4 : args[1]->NumberValue();

  // This creates the work request struct.
  uv_work_t *req = new uv_work_t();
  req->data = baton;

  // Schedule our work request with libuv. Here you can specify the functions
  // that should be executed in the threadpool and back in the main thread
  // after the threadpool function completed.
  int status = uv_queue_work(uv_default_loop(), req, HumidityWork, HumidityAfter);
  assert(status == 0);

  return Undefined();
}

// This function is executed in another thread at some point after it has been
// scheduled. IT MUST NOT USE ANY V8 FUNCTIONALITY. Otherwise your extension
// will crash randomly and you'll have a lot of fun debugging.
// If you want to use parameters passed into the original call, you have to
// convert them to PODs or some other fancy method.
void HumidityWork(uv_work_t* req) {
  Baton* baton = static_cast<Baton*>(req->data);

  // initialise the bcm2835, returning an error if this fails.
  if (!bcm2835_init()){
    baton->error_message = "Unable to initialise bcm2835";
    baton->error = true;
    return;
  }

  uint8_t retryCount;
  int i;

  #ifdef DEBUG
  uint8_t bitTimes[DHT22_DATA_BIT_COUNT];
  int32_t syncTimes[DHT22_DATA_BIT_COUNT];
  int32_t sampleTimes[DHT22_DATA_BIT_COUNT];
  int32_t ackTransition;
  int32_t ackComplete;
  #endif

  int rawTemperature = 0;
  int rawHumidity = 0;
  uint8_t checkSum = 0;

  uint8_t csPart1, csPart2, csPart3, csPart4;
  
  // configure the pin for output
  bcm2835_gpio_fsel(baton->pin, BCM2835_GPIO_FSEL_OUTP);

  // Pin needs to start HIGH, wait until it is HIGH with a timeout
  retryCount = 0;

  do {
    if (retryCount > 125) {
      baton->error_message = "DHT bus timeout"; 
      baton->error = true;
      return;
    }
    bcm2835_delayMicroseconds(2);
    retryCount++;
  } while (bcm2835_gpio_lev(baton->pin) == HIGH); // should be high

  // set the pin low 
  bcm2835_gpio_write(baton->pin, LOW);

  // hold it for 1ms
  bcm2835_delayMicroseconds(1100);

  // configure the pin for output
  bcm2835_gpio_fsel(baton->pin, BCM2835_GPIO_FSEL_INPT);

  // Find the start of the ACK Pulse
  retryCount = 0;
  
  do {
    if(retryCount > 25) { //(Spec is 20 to 40 us, 25*2 == 50 us)
      baton->error_message = "DHT not present."; 
      baton->error = true;
      return;
    } 
    bcm2835_delayMicroseconds(2);
    retryCount++;
  } while (bcm2835_gpio_lev(baton->pin) == HIGH); 
 
  // Find the end of the ACK Pulse
  retryCount = 0;

  clock_gettime(0, &baton->t_before);

  do {
    if (retryCount > 50) { //(Spec is 80 us, 50*2 == 100 us)
      baton->error_message = "DHT ack to long."; 
      baton->error = true;
      return;
    }
    bcm2835_delayMicroseconds(2);
    retryCount++;
  } while (bcm2835_gpio_lev(baton->pin) == LOW); 

  clock_gettime(0, &baton->t_after);

  #ifdef DEBUG
  ackTransition = timespec_cmp(baton->t_after, baton->t_before);
  #endif

  clock_gettime(0, &baton->t_before);

  do {
    if (retryCount > 50) { //(Spec is 80 us, 50*2 == 100 us)
      baton->error_message = "DHT ack to long."; 
      baton->error = true;
      return;
    }
    bcm2835_delayMicroseconds(2);
    retryCount++;
  } while (bcm2835_gpio_lev(baton->pin) == HIGH); 

  clock_gettime(0, &baton->t_after);

  #ifdef DEBUG
  ackComplete = timespec_cmp(baton->t_after, baton->t_before);
  #endif

  // Here sensor pulled down to start transmitting bits.

  // Read the 40 bit data stream
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {

    clock_gettime(0, &baton->t_before);

    // Find the start of the sync pulse
    retryCount = 0;
    do {
      if (retryCount > 35) { //(Spec is 50 us, 35*2 == 70 us)
        baton->error_message = "DHT sync error."; 
        baton->error = true;
        return;
      }
      bcm2835_delayMicroseconds(2);
      retryCount++;
    } while (bcm2835_gpio_lev(baton->pin) == LOW);
    
    #ifdef DEBUG
    clock_gettime(0, &baton->t_after);
    syncTimes[i] = timespec_cmp(baton->t_after, baton->t_before);
    #endif

    clock_gettime(0, &baton->t_before);

    // Measure the width of the data pulse
    retryCount = 0;
    do {
      if (retryCount > 50) { //(Spec is 80 us, 50*2 == 100 us)
        baton->error_message = "DHT data timeout error."; 
        baton->error = true;
        return;
      }
      bcm2835_delayMicroseconds(2);
      retryCount++;
    } while (bcm2835_gpio_lev(baton->pin) == HIGH);

    clock_gettime(0, &baton->t_after);

    // Identification of bit values.
    if (retryCount > 20) {
      if (i < 16) { // Humidity
        rawHumidity |= (1 << (15 - i));
      } 
      if ((i > 15) && (i < 32)) { // Temperature
        rawTemperature |= (1 << (31 - i));
      }
      if ((i > 31) && (i < 40)) { // CRC data
        checkSum |= (1 << (39 - i));
      }
    }

    #ifdef DEBUG
    bitTimes[i] = retryCount;
    sampleTimes[i] = timespec_cmp(baton->t_after, baton->t_before);
    #endif
  }

  #ifdef DEBUG
  printf("bitLoops: ");
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {
    printf("%d ", bitTimes[i]);     
  }
  printf("\n");

  printf("bitTimes: ");
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {
    printf("%d ", sampleTimes[i]);     
  }
  printf("\n");

  printf("syncTimes: ");
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {
    printf("%d ", syncTimes[i]);     
  }
  printf("\n");

  printf("Ack Pulse = %d\n", ackTransition);
  printf("Ack Pulse complete = %d\n", ackComplete);

  printf("Raw Humidity = %d\n", rawHumidity);
  printf("Raw Temperature = %d\n", rawTemperature);
  printf("Checksum = %d\n", checkSum);
  #endif

  // calculate checksum
  csPart1 = rawHumidity >> 8;
  csPart2 = rawHumidity & 0xFF;
  csPart3 = rawTemperature >> 8;
  csPart4 = rawTemperature & 0xFF;

  #ifdef DEBUG
  printf("Calculated CheckSum = %d\n", ( (csPart1 + csPart2 + csPart3 + csPart4) & 0xFF ));
  #endif

  if (checkSum != ( (csPart1 + csPart2 + csPart3 + csPart4) & 0xFF ) ) {
    baton->error_message = "DHT checksum error."; 
    baton->error = true;
    return;
  }

  // raw data to sensor values
  baton->humidity_integral = (uint8_t)(rawHumidity / 10);
  baton->humidity_decimal = (uint8_t)(rawHumidity % 10);

  // Check if temperature is below zero, non standard way of encoding negative numbers!
  if(rawTemperature & 0x8000) { 
    // Remove signal bit
    rawTemperature &= 0x7FFF; 
    baton->temperature_integral = (int8_t)(rawTemperature / 10) * -1;
  } else {
    baton->temperature_integral = (int8_t)(rawTemperature / 10);  
  }

  baton->temperature_decimal = (uint8_t)(rawTemperature % 10);  

  // set the result for the moment
  baton->result = 0;
}

// This function is executed in the main V8/JavaScript thread. That means it's
// safe to use V8 functions again. Don't forget the HandleScope!
void HumidityAfter(uv_work_t* req) {
  HandleScope scope;
  Baton* baton = static_cast<Baton*>(req->data);

  if (baton->error) {
    Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));

    // Prepare the parameters for the callback function.
    const unsigned argc = 1;
    Local<Value> argv[argc] = { err };

    // Wrap the callback function call in a TryCatch so that we can call
    // node's FatalException afterwards. This makes it possible to catch
    // the exception from JavaScript land using the
    // process.on('uncaughtException') event.
    TryCatch try_catch;
    baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
    if (try_catch.HasCaught()) {
      node::FatalException(try_catch);
    }
  } else {
    // In case the operation succeeded, convention is to pass null as the
    // first argument before the result arguments.
    // In case you produced more complex data, this is the place to convert
    // your plain C++ data structures into JavaScript/V8 data structures.
  /*
  int32_t humidity_integral;
  int32_t humidity_decimal;
  int32_t temperature_integral;
  int32_t temperature_decimal;
  */

    Local<Object> object = Object::New();
    object->Set(String::New("humidity_integral"), Number::New(baton->humidity_integral)); 
    object->Set(String::New("humidity_decimal"), Number::New(baton->humidity_decimal)); 
    object->Set(String::New("temperature_integral"), Number::New(baton->temperature_integral)); 
    object->Set(String::New("temperature_decimal"), Number::New(baton->temperature_decimal)); 

    const unsigned argc = 2;
    Local<Value> argv[argc] = {
      Local<Value>::New(Null()),
      object
    };

    // Wrap the callback function call in a TryCatch so that we can call
    // node's FatalException afterwards. This makes it possible to catch
    // the exception from JavaScript land using the
    // process.on('uncaughtException') event.
    TryCatch try_catch;
    baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
    if (try_catch.HasCaught()) {
      node::FatalException(try_catch);
    }
  }

  // The callback is a permanent handle, so we have to dispose of it manually.
  baton->callback.Dispose();

  // We also created the baton and the work_req struct with new, so we have to
  // manually delete both.
  delete baton;
  delete req;
}

void RegisterModule(Handle<Object> target) {
  target->Set(String::NewSymbol("humidity"),
      FunctionTemplate::New(Humidity)->GetFunction());
}

NODE_MODULE(raspberrysensor, RegisterModule);
