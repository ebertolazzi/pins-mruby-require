#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
  #define OS_WINDOWS
#endif

#include "mruby.h"
#include "mruby/data.h"
#include "mruby/string.h"
#include "mruby/dump.h"
#include "mruby/proc.h"
#include "mruby/compile.h"
#include "mruby/variable.h"
#include "mruby/array.h"
#include "mruby/numeric.h"

#include "opcode.h"
#include "error.h"

#ifdef OS_WINDOWS
  #include <io.h>
#else
  #include <err.h>
  #include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>


#define E_LOAD_ERROR (mrb_class_get(mrb, "LoadError"))

/* We can't use MRUBY_RELEASE_NO to determine if byte code implementation is old */
#ifdef MKOP_A
  #define USE_MRUBY_OLD_BYTE_CODE
#endif

#if MRUBY_RELEASE_NO < 10000
  mrb_value
  mrb_yield_internal(
    mrb_state * mrb,
    mrb_value   b,
    int         argc,
    mrb_value * argv,
    mrb_value   self,
    struct RClass *c
  );
  #define mrb_yield_with_class mrb_yield_internal
#endif

#if MRUBY_RELEASE_NO < 10400
  #define MRB_PROC_SET_TARGET_CLASS(p,tc) \
  do { (p)->target_class = (tc); } while (0)
#endif

#ifdef OS_WINDOWS
  #include <windows.h>
  int
  mkstemp( char *template, int mode ) {
    DWORD pathSize;
    char pathBuffer[1000];
    char tempFilename[MAX_PATH];
    UINT uniqueNum;
    pathSize = GetTempPath( 1000, pathBuffer );
    pathBuffer[ pathSize < 1000 ? pathSize : 0 ] = 0;
    uniqueNum = GetTempFileName( pathBuffer, template, 0, tempFilename );
    if ( uniqueNum == 0 ) return -1;
    strncpy( template, tempFilename, MAX_PATH );
    return _open( tempFilename, _O_RDWR|_O_BINARY, mode );
  }
#endif

#ifdef USE_MRUBY_OLD_BYTE_CODE
  static
  void
  replace_stop_with_return( mrb_state * mrb, mrb_irep * irep ) {
    if (irep->iseq[irep->ilen - 1] == MKOP_A(OP_STOP, 0)) {
      irep->iseq = mrb_realloc(mrb, irep->iseq, (irep->ilen + 1) * sizeof(mrb_code));
      irep->iseq[irep->ilen - 1] = MKOP_A(OP_LOADNIL, 0);
      irep->iseq[irep->ilen] = MKOP_AB(OP_RETURN, 0, OP_R_NORMAL);
      irep->ilen++;
    }
  }
#endif

static
int
compile_rb2mrb(
  mrb_state  * mrb0,
  const char * code,
  int          code_len,
  const char * path,
  FILE       * tmpfp
) {
  mrb_state    * mrb;
  mrbc_context * c;
  mrb_irep     * irep;
  mrb_value      result;
  int ret       = -1;
  int debuginfo = 1;

  mrb = mrb_open();
  c   = mrbc_context_new( mrb );
  c->no_exec = 1;
  if ( path != NULL ) mrbc_filename( mrb, c, path );

  result = mrb_load_nstring_cxt( mrb, code, code_len, c );
  if ( mrb_undef_p(result) ) {
    mrbc_context_free( mrb, c );
    mrb_close( mrb );
    return MRB_DUMP_GENERAL_FAILURE;
  }

  irep = mrb_proc_ptr(result)->body.irep;
  ret  = mrb_dump_irep_binary( mrb, irep, debuginfo, tmpfp );

  mrbc_context_free(mrb, c);
  mrb_close(mrb);

  return ret;
}

static
void
eval_load_irep(
  mrb_state * mrb,
  mrb_irep  * irep
) {
  int            ai;
  struct RProc * proc;

#ifdef USE_MRUBY_OLD_BYTE_CODE
  replace_stop_with_return( mrb, irep );
#endif
  proc = mrb_proc_new( mrb, irep );
  mrb_irep_decref( mrb, irep );
  MRB_PROC_SET_TARGET_CLASS( proc, mrb->object_class );

  ai = mrb_gc_arena_save( mrb );
  mrb_yield_with_class(
    mrb,
    mrb_obj_value(proc),
    0,
    NULL,
    mrb_top_self(mrb),
    mrb->object_class
  );
  mrb_gc_arena_restore( mrb, ai );
}

static
mrb_value
mrb_require_load_rb_str(
  mrb_state * mrb,
  mrb_value   self
) {
  char *path_ptr = NULL;
#ifdef OS_WINDOWS
  char tmpname[MAX_PATH] = "tmp.XXXXXXXX";
#else
  char tmpname[] = "tmp.XXXXXXXX";
  mode_t mask;
#endif
  FILE *tmpfp = NULL;
  int fd = -1, ret;
  mrb_irep *irep;
  mrb_value code, path = mrb_nil_value();

  mrb_get_args( mrb, "S|S", &code, &path );
  if ( !mrb_string_p( path ) ) path = mrb_str_new_cstr( mrb, "-" );
  path_ptr = mrb_str_to_cstr( mrb, path );

  #ifdef OS_WINDOWS
  fd = mkstemp( tmpname, 077 );
  #else
  mask = umask(077); fd = mkstemp( tmpname ); umask(mask);
  #endif
  if ( fd == -1 )
    mrb_sys_fail(
      mrb, "can't create mkstemp() at mrb_require_load_rb_str"
    );

  #ifdef OS_WINDOWS
  tmpfp = _fdopen( fd, "r+" );
  #else
  tmpfp = fdopen( fd, "r+" );
  #endif
  if ( tmpfp == NULL ) {
    close(fd);
    mrb_sys_fail(
      mrb, "can't open temporay file at mrb_require_load_rb_str"
    );
  }

  ret = compile_rb2mrb(
    mrb, RSTRING_PTR(code), RSTRING_LEN(code), path_ptr, tmpfp
  );
  if ( ret != MRB_DUMP_OK ) {
    fclose( tmpfp );
    remove( tmpname );
    mrb_raisef( mrb, E_LOAD_ERROR, "can't load file -- %S", path );
    return mrb_nil_value();
  }

  rewind( tmpfp );
  irep = mrb_read_irep_file( mrb, tmpfp );
  fclose( tmpfp );
  remove( tmpname );

  if ( irep ) {
    eval_load_irep( mrb, irep );
  } else if ( mrb->exc ) {
    // fail to load
    longjmp( *(jmp_buf*)mrb->jmp, 1 );
  } else {
    mrb_raisef( 
      mrb, E_LOAD_ERROR, "can't load file -- %S", path
    );
    return mrb_nil_value();
  }
  return mrb_true_value();
}

static
mrb_value
mrb_require_load_mrb_file( mrb_state * mrb, mrb_value self ) {
  char *path_ptr = NULL;
  FILE *fp       = NULL;
  mrb_irep *irep;
  mrb_value path;

  mrb_get_args( mrb, "S", &path );
  path_ptr = mrb_str_to_cstr( mrb, path );

  #ifdef OS_WINDOWS
  if ( 0 != fopen_s( &fp, path_ptr, "rb") )
  #else
  if ( NULL == (fp = fopen(path_ptr, "rb") ) )
  #endif
    mrb_raisef( mrb, E_LOAD_ERROR, "can't open file -- %S", path );

  irep = mrb_read_irep_file( mrb, fp );
  fclose( fp );

  if ( irep ) {
    eval_load_irep( mrb, irep );
  } else if ( mrb->exc ) { // fail to load
    longjmp( *(jmp_buf*)mrb->jmp, 1 );
  } else {
    mrb_raisef(
      mrb, E_LOAD_ERROR, "can't load file -- %S", path
    );
    return mrb_nil_value();
  }
  return mrb_true_value();
}

/*
============================================================
============================================================
============================================================
*/

static
char const *
file_basename( char const fname[] ) {
  char const * tmp ;
  char const * ptr ;

  // search last / or \\ character
  ptr = tmp = fname ;
  while ( tmp ) {
    if ( (tmp = strchr(ptr, '/' )) ||
         (tmp = strchr(ptr, '\\')) ) ptr = tmp + 1;
  }

  return ptr ;
}

#if defined(_MSC_VER) || defined(__MINGW32__)
  #ifndef PATH_MAX
    #define PATH_MAX MAX_PATH
  #endif
  #define strdup(x) _strdup(x)
#else
  #include <sys/param.h>
  #include <unistd.h>
  #include <libgen.h>
  #include <dlfcn.h>
#endif

#ifndef MAXPATHLEN
  #define MAXPATHLEN 1024
#endif

#ifndef MAXENVLEN
  #define MAXENVLEN 1024
#endif

#ifdef OS_WINDOWS

  #include <windows.h>

  static
  int
  relativeToFullPath( char const path[], char full_path[MAXPATHLEN] ) {
    DWORD retval = GetFullPathNameA( path, MAXPATHLEN, full_path, NULL );
    return retval > 0 && retval < MAXPATHLEN ;
  }

  static
  int
  GetEnvironmentToString( char const envName[], char out[], unsigned len ) {
    DWORD n = GetEnvironmentVariableA(envName,out,(DWORD)len);
    return n > 0 && n < (DWORD)len ;
  }

  void
  CheckError( char const lib[], mrb_state * mrb ) {
    // Get the error message, if any.
    DWORD errorMessageID = GetLastError();
    if ( errorMessageID == 0 ) return ; // No error message has been recorded
    printf("errorMessageID: %ld\n", errorMessageID);

    LPSTR messageBuffer = NULL;
    size_t size = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                  FORMAT_MESSAGE_FROM_SYSTEM |
                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL,
                                  errorMessageID,
                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  (LPSTR)&messageBuffer,
                                  0,
                                  NULL );

    printf("failed to load DLL: %s\n", lib);
    mrb_raise( mrb, E_RUNTIME_ERROR, messageBuffer ) ;
    // Free the buffer.
    LocalFree(messageBuffer);
  }

#else

  #include <dlfcn.h>

  static
  int
  relativeToFullPath( char const path[], char full_path[MAXPATHLEN] ) {
    return realpath(path, full_path) != NULL ;
  }

  static
  int
  GetEnvironmentToString( char const envName[], char out[], unsigned len ) {
    char const * ptr = getenv( envName ) ;
    int ok = ptr != NULL && strlen( ptr ) < len ;
    if ( ok ) strcpy( out, ptr ) ;
    return ok ;
  }

  void
  CheckError( char const lib[], mrb_state * mrb ) {
    char const * err = dlerror() ;
    if ( err != NULL ) {
      printf("failed to load DLL: %s\n", lib);
      mrb_raise( mrb, E_RUNTIME_ERROR, dlerror() );
    }
  }

#endif

// workaround for new mruby version
#if MRUBY_RELEASE_MAJOR <= 1 && MRUBY_RELEASE_MINOR <= 3
  #define TARGET_CLASS(PROC) PROC->target_class
#else
  #define TARGET_CLASS(PROC) PROC->e.target_class
#endif

static
void
mrb_load_irep_data( mrb_state * mrb, const uint8_t* data ) {

  //printf( "require:mrb_load_irep_data\n") ;

  int ai = mrb_gc_arena_save(mrb);
  mrb_irep *irep = mrb_read_irep(mrb,data);
  mrb_gc_arena_restore(mrb,ai);

  if (irep) {
    #ifdef USE_MRUBY_OLD_BYTE_CODE
    replace_stop_with_return(mrb, irep);
    #endif
    struct RProc *proc = mrb_proc_new(mrb, irep);
    TARGET_CLASS(proc) = mrb->object_class; // changed RProc with a union

    int ai = mrb_gc_arena_save(mrb);
    mrb_yield_with_class( mrb,
                          mrb_obj_value(proc),
                          0,
                          NULL,
                          mrb_top_self(mrb),
                          mrb->object_class );
    mrb_gc_arena_restore(mrb, ai);
  } else if (mrb->exc) {
    // fail to load
    longjmp(*(jmp_buf*)mrb->jmp, 1);
  }
}

mrb_value
mrb_require_load_file( mrb_state * mrb, mrb_value self ) {
  char entry[PATH_MAX]      = {0};
  char entry_irep[PATH_MAX] = {0};
  typedef void (*fn_mrb_gem_init)(mrb_state *mrb);
  mrb_value mrb_filepath;

  mrb_get_args( mrb, "S", &mrb_filepath );
  char const * filepath = mrb_str_to_cstr( mrb, mrb_filepath );

  //printf( "require:load_so_file: %s\n", filepath) ;

  #ifdef OS_WINDOWS
  HMODULE handle = LoadLibrary(filepath);
  #else
  void * handle = dlopen(filepath, RTLD_LAZY|RTLD_GLOBAL);
  #endif

  if ( handle == NULL ) {
    //printf( "require:load_so_file: null handle, check error\n" ) ;
    CheckError( filepath, mrb ) ;
    char message[1024] ;
    snprintf( message, 1023, "failed to load %s, open return a NULL pointer\n", filepath );
    printf( "%s", message ) ;
    mrb_raise(mrb, E_LOAD_ERROR, message );
  }

  char * ptr = strdup(file_basename(filepath)) ;
  char * tmp = strrchr(ptr, '.');
  if (tmp) *tmp = 0;
  for ( tmp = ptr ; *tmp ; ++tmp ) { if (*tmp == '-') *tmp = '_' ; }

  snprintf(entry,      sizeof(entry)-1,      "mrb_%s_gem_init",    ptr);
  snprintf(entry_irep, sizeof(entry_irep)-1, "gem_mrblib_irep_%s", ptr);
  free(ptr);

  //printf( "require:load_so_file attach entry\n") ;

  #ifdef OS_WINDOWS
  FARPROC addr_entry      = GetProcAddress(handle, entry);
  FARPROC addr_entry_irep = GetProcAddress(handle, entry_irep);
  #else
  void * addr_entry      = dlsym(handle, entry);
  void * addr_entry_irep = dlsym(handle, entry_irep);
  #endif

  if ( addr_entry == NULL && addr_entry_irep == NULL ) {
    char message[1024] ;
    snprintf( message, 1023, "failed to attach %s or %s in library %s\n",
              entry, entry_irep, filepath );
    printf( "%s", message ) ;
    mrb_raise(mrb, E_LOAD_ERROR, message );
  }

  if ( addr_entry != NULL ) {
    printf( "Attach %s from library %s\n", entry, filepath );
    fn_mrb_gem_init fn = (fn_mrb_gem_init) addr_entry;
    int ai = mrb_gc_arena_save(mrb);
    fn(mrb);
    mrb_gc_arena_restore(mrb, ai);
  }

  if ( addr_entry_irep != NULL ) {
    printf( "Attach %s from library %s\n", entry_irep, filepath );
    uint8_t const * data = (uint8_t const *) addr_entry_irep;
    mrb_load_irep_data( mrb, data );
  }

  return mrb_true_value();
}

void
mrb_pins_mruby_require_gem_init( mrb_state * mrb ) {
  struct RClass *krn;
  krn = mrb->kernel_module;

  mrb_define_method(
    mrb, krn, "___load_rb_str",
    mrb_require_load_rb_str,
    MRB_ARGS_ANY()
  );

  mrb_define_method(
    mrb, krn, "___load_mrb_file",
    mrb_require_load_mrb_file,
    MRB_ARGS_REQ(1)
  );

  mrb_define_method(
    mrb, krn, "___load_shared_file",
    mrb_require_load_file,
    MRB_ARGS_REQ(1)
  );
}

void
mrb_pins_mruby_require_gem_final( mrb_state * mrb )
{
}
