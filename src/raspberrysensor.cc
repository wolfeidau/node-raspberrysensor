/*
 * raspberrysensor
 * https://github.com/markw/raspberrysensor
 *
 * Access a temperature and humidity sensor using the GPIO ports on the Raspberry Pi
 *
 * Copyright (c) 2012 Mark Wolfe
 * Licensed under the MIT license.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/io.h>
#include <bcm2835.h>

#define DHT22_DATA_BIT_COUNT 41

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
Handle<Value> Async(const Arguments& args);
void AsyncWork(uv_work_t* req);
void AsyncAfter(uv_work_t* req);

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
  int32_t temp;
  int32_t humidity;

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
Handle<Value> Async(const Arguments& args) {
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
  int status = uv_queue_work(uv_default_loop(), req, AsyncWork, AsyncAfter);
  assert(status == 0);

  return Undefined();
}

// This function is executed in another thread at some point after it has been
// scheduled. IT MUST NOT USE ANY V8 FUNCTIONALITY. Otherwise your extension
// will crash randomly and you'll have a lot of fun debugging.
// If you want to use parameters passed into the original call, you have to
// convert them to PODs or some other fancy method.
void AsyncWork(uv_work_t* req) {
  Baton* baton = static_cast<Baton*>(req->data);

  // initialise the bcm2835, returning an error if this fails.
  if (!bcm2835_init()){
    baton->error_message = "Unable to initialise bcm2835";
    baton->error = true;
    return;
  }

//  struct sched_param param;
  uint8_t retryCount;
  uint8_t bitTimes[DHT22_DATA_BIT_COUNT];
  int i;

  int currentTemperature;
  int currentHumidity;
  
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
  bcm2835_delay(1);

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
  } while (bcm2835_gpio_lev(baton->pin) == LOW); 
 
  // Find the end of the ACK Pulse
  retryCount = 0;

  do {
    if (retryCount > 50) { //(Spec is 80 us, 50*2 == 100 us)
      baton->error_message = "DHT not present."; 
      baton->error = true;
      return;
    }
    bcm2835_delayMicroseconds(2);
    retryCount++;
  } while (bcm2835_gpio_lev(baton->pin) == HIGH); 

  // Read the 40 bit data stream
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {

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

    // assign the bit value
    bitTimes[i] = retryCount;
  }

  // DEBUG
  printf("values: ");
  for(i = 0; i < DHT22_DATA_BIT_COUNT; i++) {
    printf("%d ", bitTimes[i]);     
  }
  printf("\n");
  // DEBUG

  // Spec: 0 is 26 to 28 us
  // Spec: 1 is 70 us
  // bitTimes[x] <= 11 is a 0
  // bitTimes[x] >  11 is a 1 

  // read humidity  
  currentHumidity = 0;
  for(i = 0; i < 16; i++) {
    if(bitTimes[i + 1] > 11) {
      currentHumidity |= (1 << (15 - i));
    }  
  }

  baton->humidity = currentHumidity & 0x7FFF;

  // DEBUG
  printf("currentHumidity = %d\n", baton->humidity);
  // DEBUG

  // read the temperature
  currentTemperature = 0;
  for(i = 0; i < 16; i++) {
    if(bitTimes[i + 17] > 11){
      currentTemperature |= (1 << (15 - i));
    }
  }

  baton->temp = currentTemperature & 0x7FFF;

  // DEBUG
  printf("currentTemperature = %d\n", baton->temp);
  // DEBUG

/*
  clock_gettime(0, &baton->t_before);

  retryCount = 0;

  do {

    if(retryCount > 100){
      clock_gettime(0, &baton->t_after);
      baton->result = timespec_cmp(baton->t_after, baton->t_before);
      return; 
    }
    
    bcm2835_delayMicroseconds(10);
    retryCount++;

  } while (1);
*/
  // Do work in threadpool here.
  baton->result = baton->pin;

  // If the work we do fails, set baton->error_message to the error string
  // and baton->error to true.
}

// This function is executed in the main V8/JavaScript thread. That means it's
// safe to use V8 functions again. Don't forget the HandleScope!
void AsyncAfter(uv_work_t* req) {
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
    const unsigned argc = 2;
    Local<Value> argv[argc] = {
      Local<Value>::New(Null()),
      Local<Value>::New(Integer::New(baton->result))
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
  target->Set(String::NewSymbol("async"),
      FunctionTemplate::New(Async)->GetFunction());
}

NODE_MODULE(raspberrysensor, RegisterModule);
