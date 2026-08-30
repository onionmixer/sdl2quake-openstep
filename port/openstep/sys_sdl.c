/*
 * sys_sdl.c -- system layer
 *
 * From sdlquake (https://github.com/mckayemu/sdlquake), which is
 * id Software's Quake under the GNU General Public License, version 2
 * or later.  MODIFIED for OPENSTEP 4.2 and SDL2 on 2026-08-30:
 *
 *   - dropped sys/ipc.h, sys/shm.h and sys/mman.h, which this file includes
 *     and never uses, and which OPENSTEP does not have
 *   - made the no-op moncontrol() static: OPENSTEP's System framework
 *     exports the real one and the two collided at link time
 *   - gave -heapsize an implementation and raised the default heap from
 *     8 MiB to 16 MiB
 *   - replaced Sys_MakeCodeWriteable's mprotect() body with a refusal that
 *     names what OPENSTEP would need instead (vm_protect); nothing calls
 *     it while id386 is 0
 *
 * The unmodified original is kept beside this tree in upstream/sdlquake.
 */
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifndef __WIN32__
/*
 * OPENSTEP: sys/ipc.h, sys/shm.h and sys/mman.h are gone from here.
 *
 * They are System V shared memory and BSD mmap, and this file uses NEITHER
 * -- checked by grepping for every name they provide (shmget, shmat,
 * shmctl, key_t, IPC_*, SHM_*, ftok, mmap, munmap, PROT_*): not one appears.
 * They are leftovers from the Linux ancestry, and OPENSTEP has no SysV IPC
 * headers at all, so keeping them would fail the compile for nothing.
 */
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include "quakedef.h"

qboolean			isDedicated;

int noconinput = 0;

char *basedir = ".";
char *cachedir = "/tmp";

cvar_t  sys_linerefresh = {"sys_linerefresh","0"};// set for entity display
cvar_t  sys_nostdout = {"sys_nostdout","0"};

// =======================================================================
// General routines
// =======================================================================

void Sys_DebugNumber(int y, int val)
{
}

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	
	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	fprintf(stderr, "%s", text);
	
	//Con_Print (text);
}

void Sys_Quit (void)
{
	Host_Shutdown();
	exit(0);
}

void Sys_Init(void)
{
#if id386
	Sys_SetFPCW();
#endif
}

#if !id386

/*
================
Sys_LowFPPrecision
================
*/
void Sys_LowFPPrecision (void)
{
// causes weird problems on Nextstep
}


/*
================
Sys_HighFPPrecision
================
*/
void Sys_HighFPPrecision (void)
{
// causes weird problems on Nextstep
}

#endif	// !id386


void Sys_Error (char *error, ...)
{ 
    va_list     argptr;
    char        string[1024];

    va_start (argptr,error);
    vsprintf (string,error,argptr);
    va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);

	Host_Shutdown ();
	exit (1);

} 

void Sys_Warn (char *warning, ...)
{ 
    va_list     argptr;
    char        string[1024];
    
    va_start (argptr,warning);
    vsprintf (string,warning,argptr);
    va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
} 

/*
===============================================================================

FILE IO

===============================================================================
*/

#define	MAX_HANDLES		10
FILE	*sys_handles[MAX_HANDLES];

int		findhandle (void)
{
	int		i;
	
	for (i=1 ; i<MAX_HANDLES ; i++)
		if (!sys_handles[i])
			return i;
	Sys_Error ("out of handles");
	return -1;
}

/*
================
Qfilelength
================
*/
static int Qfilelength (FILE *f)
{
	int		pos;
	int		end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	FILE	*f;
	int		i;
	
	i = findhandle ();

	f = fopen(path, "rb");
	if (!f)
	{
		*hndl = -1;
		return -1;
	}
	sys_handles[i] = f;
	*hndl = i;
	
	return Qfilelength(f);
}

int Sys_FileOpenWrite (char *path)
{
	FILE	*f;
	int		i;
	
	i = findhandle ();

	f = fopen(path, "wb");
	if (!f)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));
	sys_handles[i] = f;
	
	return i;
}

void Sys_FileClose (int handle)
{
	if ( handle >= 0 ) {
		fclose (sys_handles[handle]);
		sys_handles[handle] = NULL;
	}
}

void Sys_FileSeek (int handle, int position)
{
	if ( handle >= 0 ) {
		fseek (sys_handles[handle], position, SEEK_SET);
	}
}

int Sys_FileRead (int handle, void *dst, int count)
{
	char *data;
	int size, done;

	size = 0;
	if ( handle >= 0 ) {
		data = dst;
		while ( count > 0 ) {
			done = fread (data, 1, count, sys_handles[handle]);
			if ( done == 0 ) {
				break;
			}
			data += done;
			count -= done;
			size += done;
		}
	}
	return size;
		
}

int Sys_FileWrite (int handle, void *src, int count)
{
	char *data;
	int size, done;

	size = 0;
	if ( handle >= 0 ) {
		data = src;
		while ( count > 0 ) {
			done = fread (data, 1, count, sys_handles[handle]);
			if ( done == 0 ) {
				break;
			}
			data += done;
			count -= done;
			size += done;
		}
	}
	return size;
}

int	Sys_FileTime (char *path)
{
	FILE	*f;
	
	f = fopen(path, "rb");
	if (f)
	{
		fclose(f);
		return 1;
	}
	
	return -1;
}

void Sys_mkdir (char *path)
{
#ifdef __WIN32__
    mkdir (path);
#else
    mkdir (path, 0777);
#endif
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    va_list argptr; 
    static char data[1024];
    FILE *fp;
    
    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
    fp = fopen(file, "a");
    fwrite(data, strlen(data), 1, fp);
    fclose(fp);
}

double Sys_FloatTime (void)
{
#ifdef __WIN32__

	static int starttime = 0;

	if ( ! starttime )
		starttime = clock();

	return (clock()-starttime)*1.0/1024;

#else

    struct timeval tp;
    struct timezone tzp; 
    static int      secbase; 
    
    gettimeofday(&tp, &tzp);  

    if (!secbase)
    {
        secbase = tp.tv_sec;
        return tp.tv_usec/1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec/1000000.0;

#endif
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void alarm_handler(int x)
{
	oktogo=1;
}

byte *Sys_ZoneBase (int *size)
{

	char *QUAKEOPT = getenv("QUAKEOPT");

	*size = 0xc00000;
	if (QUAKEOPT)
	{
		while (*QUAKEOPT)
			if (tolower(*QUAKEOPT++) == 'm')
			{
				*size = atof(QUAKEOPT) * 1024*1024;
				break;
			}
	}
	return malloc (*size);

}

void Sys_LineRefresh(void)
{
}

void Sys_Sleep(void)
{
	SDL_Delay(1);
}

void floating_point_exception_handler(int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

/*
 * OPENSTEP: static, because the system already exports this name.
 *
 * Upstream defines a no-op moncontrol() as a placeholder for the profiling
 * hook the Linux build had.  OPENSTEP's System framework defines the real
 * one in gmon.o, so an exported stub collides at link time:
 *
 *   /bin/ld: multiple definitions of symbol _moncontrol
 *
 * Making ours static keeps upstream's intent exactly -- main() still calls a
 * no-op -- while leaving the system's symbol alone.  Deleting the call
 * instead would work too, but would quietly change what a profiling build
 * does, and this file is not the place to decide that.
 */
static void moncontrol(int x)
{
}

static volatile int sys_term_asked;

static void sys_term_handler (int sig)
{
	(void)sig;
	sys_term_asked = 1;
}

int main (int c, char **v)
{

	double		time, oldtime, newtime;
	quakeparms_t parms;
	extern int vcrFile;
	extern int recording;
	static int frame;

	/*
	 * OPENSTEP: stdout unbuffered.
	 *
	 * Not a fix for anything -- a diagnostic that makes the log's last line
	 * mean what it appears to mean.  With block buffering, two runs of the
	 * same crash ended on different lines, because what survives is the last
	 * FLUSH rather than the last thing printed.  Localising a fault from
	 * that is localising from noise.
	 *
	 * Costs a write per line.  On a port whose failures are still being
	 * found, that is worth paying.
	 */
	setbuf (stdout, (char *)0);

	moncontrol(0);

//	signal(SIGFPE, floating_point_exception_handler);
	signal(SIGFPE, SIG_IGN);
	/*
	 * A SIGTERM must reach Host_Shutdown: IN_Shutdown puts the mouse
	 * acceleration table back.  The handler only sets a flag -- the
	 * frame loop quits -- because nothing async-safe can talk to the
	 * event system.  glquake installs its own handler later (VID_Init)
	 * and overrides this one; squake had none and died restoring
	 * nothing.
	 */
	signal(SIGTERM, sys_term_handler);

	/*
	 * OPENSTEP: the heap is a command-line argument again, and its default
	 * is larger.
	 *
	 * Upstream fixes this at 8 MiB and never reads -heapsize, which is the
	 * size id shipped for a 1996 machine with 16 MiB of RAM.  This one has
	 * 504 MiB, and a hunk that small is what makes a big map fail to load
	 * with a message about "not enough RAM allocated" rather than anything
	 * to do with the map.
	 *
	 * 16 MiB is the default here rather than something larger because the
	 * hunk is allocated up front and never grows: taking more than a map
	 * needs costs a user nothing on this machine, but it is still a number
	 * that should be chosen deliberately rather than raised until the
	 * symptom goes away.  -heapsize overrides it, in kilobytes, as every
	 * other Quake does.
	 */
	parms.memsize = 16*1024*1024;
	{
		/*
		 * Read from c/v DIRECTLY, not through COM_CheckParm.
		 *
		 * COM_CheckParm reads com_argc/com_argv, and those are filled by
		 * COM_InitArgv -- which this function does not call until after the
		 * heap has been allocated.  Asking it here would read two empty
		 * globals and silently never find the argument.
		 */
		int a;
		for (a = 1; a < c - 1; a++)
		{
			if (strcmp (v[a], "-heapsize") == 0)
			{
				int kb = atoi (v[a+1]);
				if (kb > 0)
					parms.memsize = kb * 1024;
				break;
			}
		}
	}
	parms.membase = malloc (parms.memsize);
	if (!parms.membase)
		Sys_Error ("Could not allocate %d bytes for the heap "
			   "-- try a smaller -heapsize", parms.memsize);
	parms.basedir = basedir;
	/*
	 * NO CACHE DIRECTORY.
	 *
	 * The Linux original set this to /tmp, where Quake copies every file it
	 * opens and then reads the COPY (common.c:1440-1455).  Here that is all
	 * cost and one real hazard: /tmp is emptied at every boot, so the copies
	 * are remade constantly -- and a run that ends while a copy is in
	 * progress leaves a SHORT file that the next run reads as though it were
	 * whole.  A zero-length glquake/v_shot.ms2 in that cache is what a core
	 * dump was traced to: GL_MakeAliasModelDisplayLists read numcommands and
	 * numorder out of an empty file and GL_DrawAliasFrame then walked a
	 * display list that was not there.
	 *
	 * The data is on a local disk already.  Copying it to another local
	 * directory buys nothing.
	 */
	parms.cachedir = "";

	COM_InitArgv(c, v);
	parms.argc = com_argc;
	parms.argv = com_argv;

	Sys_Init();

    Host_Init(&parms);

	Cvar_RegisterVariable (&sys_nostdout);

    oldtime = Sys_FloatTime () - 0.1;
    while (1)
    {
        if (sys_term_asked)
            Sys_Quit ();

// find time spent rendering last frame
        newtime = Sys_FloatTime ();
        time = newtime - oldtime;

        if (cls.state == ca_dedicated)
        {   // play vcrfiles at max speed
            if (time < sys_ticrate.value && (vcrFile == -1 || recording) )
            {
                SDL_Delay (1);
                continue;       // not time to run a server only tic yet
            }
            time = sys_ticrate.value;
        }

        if (time > sys_ticrate.value*2)
            oldtime = newtime;
        else
            oldtime += time;

        if (++frame > 10)
            moncontrol(1);      // profile only while we do each Quake frame
        Host_Frame (time);
        moncontrol(0);

// graphic debugging aids
        if (sys_linerefresh.value)
            Sys_LineRefresh ();
    }

}


/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	/*
	 * NOT IMPLEMENTED, and it says so rather than pretending.
	 *
	 * Upstream calls mprotect(), which OPENSTEP does not have -- this is
	 * Mach, where the equivalent is vm_protect() on task_self().  Writing
	 * that here would be five lines, and they would be five lines nobody
	 * can run: every caller of this function is inside `#if id386`
	 * (r_main.c:234, 468, 475 and d_modech.c:45), and this port builds with
	 * id386 = 0, so the assembly that wants writable code is not there.
	 *
	 * Untested code that looks finished is worse than a refusal that names
	 * what is missing.  If the hand-written assembly is ever enabled here,
	 * this is the function to write, and it should use vm_protect with
	 * VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE over the page-aligned
	 * range -- the same rounding upstream does with getpagesize().
	 */
	Sys_Error ("Sys_MakeCodeWriteable: not implemented on OPENSTEP.\n"
		   "This build has id386 = 0, so nothing should ask for it; "
		   "enabling the assembly needs a vm_protect implementation here.");
}

