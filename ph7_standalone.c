#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ph7.h"

#define PHP_ROUTER "<?php include(getcwd() . $_SERVER['PATH_INFO']); ?>"
#define INPUT_BUFFER 8192

static void EndHeaders() {
  puts("X-Powered-By: " PH7_SIG "\n\n");
}

static void Fatal (int status, const char *zMsg) {
  if (status) {
    puts("HTTP/1.1 404 Not Found\n");
  } else {
    puts("HTTP/1.1 500 Internal Server Error\n");
  }
  puts("Content-Type: text/plain; charset=utf-8\n");
  EndHeaders();
  ph7_lib_shutdown();
  exit(1);
}

/*
* VM output consumer callback.
* Each time the virtual machine generates some outputs, the following
* function gets called by the underlying virtual machine to consume
* the generated output.
* All this function does is redirecting the VM output to STDOUT.
* This function is registered later via a call to ph7_vm_config()
* with a configuration verb set to: PH7_VM_CONFIG_OUTPUT.
*/
static int OutputConsumer(const void *pOutput, unsigned int nOutputLen, void *pUserData /* Unused */)
{
  /*
   * Note that it's preferable to use the write() system call to display the output
   * rather than using the libc printf() which everybody now is extremely slow.
   */

  int cnt;
  while (nOutputLen > 0) {
    cnt = write(STDOUT_FILENO, (const char*) pOutput, nOutputLen);
    if (cnt <= 0) break;
    nOutputLen -= cnt;
  }
  return PH7_OK;
}

int main(int argc, char **argv) {
  ph7 *pEngine; /* PH7 engine */
  ph7_vm *pVm; /* Compiled PHP program */
  int err = -1;

  err = ph7_init(&pEngine);
  if (err != PH7_OK) {
    Fatal(0, "Unable to allocate a new PH7 engine instance");
  }

  ph7_config(pEngine, PH7_CONFIG_ERR_OUTPUT, OutputConsumer, NULL);

  /* Compile the PHP router program defined above */
  err = ph7_compile_v2(
      pEngine, /* PH7 engine */
      PHP_ROUTER, /* PHP router program */
      -1 /* Compute input length automatically*/,
      &pVm, /* OUT: Compiled PHP program */
      0 /* IN: Compile flags */
   );
  if (err != PH7_OK) {
    if (err == PH7_COMPILE_ERR) {
      const char *zErrLog;
      int nLen;
      /* Extract error log */
      ph7_config(pEngine,
        PH7_CONFIG_ERR_LOG,
        &zErrLog,
        &nLen
      );
      if (nLen > 0) {
        /* zErrLog is null terminated */
	puts(zErrLog);
      }
    }

    /* Exit */
    Fatal(1, "Compile error");
  }

  /*
   * Now we have our script compiled, it's time to configure our VM.
   * We will install the output consumer callback defined above
   * so that we can consume and redirect the VM output to STDOUT.
   */
  err = ph7_vm_config(pVm,
      PH7_VM_CONFIG_OUTPUT,
      OutputConsumer, /* Output Consumer callback */
      0 /* Callback private data */
  );

  if(err != PH7_OK) {
     Fatal(1, "Error while installing the VM output consumer callback");
  }

  /* Now configure the VM
   */
  char req[INPUT_BUFFER];
  int lines = -1;
  int acc = 0;
  while (1) {
    lines = read(STDIN_FILENO, req + acc, INPUT_BUFFER - acc);
    if (lines <= 0) break;
    acc += lines;
  }

  if (lines < 0) {
    Fatal(0, "Unable to read stdin");
  }

  ph7_vm_config(pVm,PH7_VM_CONFIG_HTTP_REQUEST, req, acc);
  ph7_vm_config(pVm,PH7_VM_CONFIG_ERR_REPORT);

  /*
   * And finally, execute our program. Note that your output (STDOUT in our case)
   * should display the result.
   */
   ph7_vm_exec(pVm,0);

   /* All done, cleanup the mess left behind.
    */
   ph7_vm_release(pVm);
   ph7_release(pEngine);
   
   return 0;
}
