/**
	@file
	hexwave~: a simple audio object for Max
	original by: jeremy bernstein, jeremy@bootsquad.com
	@ingroup examples
*/

#include "ext.h"			// standard Max include, always required (except in Jitter)
#include "ext_obex.h"		// required for "new" style objects
#include "z_dsp.h"			// required for MSP objects
#include "ext_atomic.h"

#define STB_HEXWAVE_IMPLEMENTATION
#include "stb_hexwave.h"

// struct to represent the object's state
typedef struct _hexwave {
	t_pxobject		ob;			// the object itself (t_pxobject in MSP instead of t_object)
    HexWave         wave;
    double          freq;
    long             reflect;
    double           peak_time;
    double           half_height;
    double           zero_wait;
    double          samplerate;
    short l_fcon;            // is a signal connected to the left inlet
} t_hexwave;


// method prototypes
void *hexwave_new(t_symbol *s, long argc, t_atom *argv);
void hexwave_free(t_hexwave *x);
void hexwave_assist(t_hexwave *x, void *b, long m, long a, char *s);
void hexwave_float(t_hexwave *x, double f);
void hexwave_int(t_hexwave *x, long f);
void hexwave_dsp64(t_hexwave *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void hexwave_perform64(t_hexwave *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);


// global class pointer variable
static t_class *hexwave_class = NULL;


//***********************************************************************************************

void ext_main(void *r)
{
	// object initialization, note the use of dsp_free for the freemethod, which is required
	// unless you need to free allocated memory, in which case you should call dsp_free from
	// your custom free function.

	t_class *c = class_new("hexwave~", (method)hexwave_new, (method)dsp_free, (long)sizeof(t_hexwave), 0L, A_GIMME, 0);

    class_addmethod(c, (method)hexwave_float,               "float",     A_FLOAT, 0);
    class_addmethod(c, (method)hexwave_int,                 "int",     A_LONG, 0);
	class_addmethod(c, (method)hexwave_dsp64,		        "dsp64",    A_CANT, 0);
	class_addmethod(c, (method)hexwave_assist,	            "assist",	A_CANT, 0);

	class_dspinit(c);
	class_register(CLASS_BOX, c);
	hexwave_class = c;
}


void *hexwave_new(t_symbol *s, long argc, t_atom *argv)
{
    
    post("hexwave~ based on stb_hexwave.h by Sean Barrett (nothings.org). external v1.0 compiled by miunau (miunau.com).");
	t_hexwave *x = (t_hexwave *)object_alloc(hexwave_class);

	if (x) {
		dsp_setup((t_pxobject *)x, 5);	// MSP inlets: arg is # of inlets and is REQUIRED!
		// use 0 if you don't need inlets
        x->freq = atom_getfloatarg(0,argc,argv);
        x->reflect = atom_getintarg(1,argc,argv);
		x->peak_time = atom_getfloatarg(2,argc,argv);
        x->half_height = atom_getfloatarg(3,argc,argv);
        x->zero_wait = atom_getfloatarg(4,argc,argv);
        
        int blep = atom_getintarg(5,argc,argv);
        int oversample = atom_getintarg(6,argc,argv);

        hexwave_init(blep ? blep : 32, oversample ? oversample : 4, NULL);
        hexwave_create(&x->wave, x->reflect, x->peak_time, x->half_height, x->zero_wait);
        outlet_new(x, "signal");         // signal outlet (note "signal" rather than NULL)
	}
	return (x);
}


void hexwave_free(t_hexwave *x)
{
    dsp_free((t_pxobject *)x);
}


void hexwave_assist(t_hexwave *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { //inlet
        if(a == 0) {
            sprintf(s, "Frequency (Hz)");
        }
        else if(a == 1) {
            sprintf(s, "Reflect (boolean, 0 or 1)");
        }
        else if(a == 2) {
            sprintf(s, "Peak time (float, 0..1)");
        }
        else if(a == 3) {
            sprintf(s, "Half height (float, -1..1)");
        }
        else if(a == 4) {
            sprintf(s, "Zero wait (float, 0..1)");
        }
	}
	else {	// outlet
		sprintf(s, "Signal out");
	}
}


void hexwave_float(t_hexwave *x, double f)
{
    long in = proxy_getinlet((t_object *)x);
    post("float: %f, in: %i", f, in);

    if (in == 0) {
        x->freq = f;
    }
    else if (in == 2) {
        x->peak_time = f;
    }
    else if (in == 3) {
        x->half_height = f;
    }
    else if (in == 4) {
        x->zero_wait = f;
    }

}


void hexwave_int(t_hexwave *x, long f)
{
    long in = proxy_getinlet((t_object *)x);
    
    if (in == 1) {
        x->reflect = f;
    }
}


// registers a function for the signal chain in Max
void hexwave_dsp64(t_hexwave *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	//post("my sample rate is: %f", samplerate);

	// instead of calling dsp_add(), we send the "dsp_add64" message to the object representing the dsp chain
	// the arguments passed are:
	// 1: the dsp64 object passed-in by the calling function
	// 2: the symbol of the "dsp_add64" message we are sending
	// 3: a pointer to your object
	// 4: a pointer to your 64-bit perform method
	// 5: flags to alter how the signal chain handles your object -- just pass 0
	// 6: a generic pointer that you can use to pass any additional data to your perform method

    post("count %i", count[0]);
    x->l_fcon = count[0];    // signal connected to the frequency inlet?
    x->samplerate = samplerate;
	object_method(dsp64, gensym("dsp_add64"), x, hexwave_perform64, 0, NULL);
}


// this is the 64-bit perform method audio vectors
void hexwave_perform64(t_hexwave *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double *in = ins[0];        // we get audio for each inlet of the object from the **ins argument
    t_double *out = outs[0];

    int n = sampleframes;
    
    //post("in: %i %f", x->l_fcon, in[0]);
    //post("sample rate: %f", (float)x->samplerate);
    //post("samples: %i", sampleframes);
    
    hexwave_change(&x->wave, x->reflect, x->peak_time, x->half_height, x->zero_wait);
    double_t volscale = 1.0 - fabs(x->half_height / 4);

    t_float samples[sampleframes];
    
    for(int i=0; i < n; i++) {
        samples[i] = 0.0f;
    }

    hexwave_generate_samples(samples, n, &x->wave, x->freq / x->samplerate);
    
    for(int i=0; i < n; i++) {
        if(x->l_fcon) {
            out[i] = (double)samples[i] * (1.0f - in[i]) * volscale;
        }
        else {
            out[i] = (double)samples[i] * volscale;
        }
    }
    
}

