/*============================================================================
Copyright (c) 2018-2025 Raspberry Pi Holdings Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
============================================================================*/

#include <glib/gi18n.h>

#include "lxutils.h"

#include "gpu.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static float get_gpu_usage (GPUPlugin *g);
static gboolean gpu_update (GPUPlugin *g);

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

static float get_gpu_usage (GPUPlugin *g)
{
    char *buf = NULL;
    size_t res = 0;
    unsigned long jobs, active;
    unsigned long long timestamp, elapsed = 0, runtime;
    float max, load[5];
    int i;

    // open the stats file
    FILE *fp = fopen ("/sys/kernel/debug/dri/0/gpu_usage", "rb");
    if (fp == NULL) fp = fopen ("/sys/kernel/debug/dri/1/gpu_usage", "rb");
    if (fp == NULL) return 0.0;

    // read the stats file a line at a time
    while (getline (&buf, &res, fp) > 0)
    {
        if (sscanf (buf, "timestamp;%lld;", &timestamp) == 1)
        {
            // use the timestamp line to calculate time since last measurement
            elapsed = timestamp - g->last_timestamp;
            g->last_timestamp = timestamp;
        }
        else if (sscanf (strchr (buf, ';'), ";%ld;%lld;%ld;", &jobs, &runtime, &active) == 3)
        {
            // depending on which queue is in the line, calculate the percentage of time used since last measurement
            // store the current time value for the next calculation
            i = -1;
            if (!strncmp (buf, "v3d_bin", 7)) i = 0;
            if (!strncmp (buf, "v3d_ren", 7)) i = 1;
            if (!strncmp (buf, "v3d_tfu", 7)) i = 2;
            if (!strncmp (buf, "v3d_csd", 7)) i = 3;
            if (!strncmp (buf, "v3d_cac", 7)) i = 4;

            if (i != -1)
            {
                if (g->last_val[i] == 0) load[i] = 0.0;
                else
                {
                    if (elapsed)
                    {
                        load[i] = runtime;
                        load[i] -= g->last_val[i];
                        load[i] /= elapsed;
                    }
                }
                g->last_val[i] = runtime;
            }
        }
    }

    // list is now filled with calculated loadings for each queue for each PID
    free (buf);
    fclose (fp);

    // calculate the max of the five queue values and store in the task array
    max = 0.0;
    for (i = 0; i < 5; i++)
        if (load[i] > max)
            max = load[i];

    return max;
}

/* Periodic timer callback */

static gboolean gpu_update (GPUPlugin *g)
{
    char buffer[256];
    float gpu_val;

    if (g_source_is_destroyed (g_main_current_source ())) return FALSE;

    gpu_val = get_gpu_usage (g);
    if (g->show_percentage) sprintf (buffer, "G:%3.0f", gpu_val * 100.0);
    else buffer[0] = 0;
    graph_new_point (&(g->graph), gpu_val, 0, buffer);

    return TRUE;
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for system config changed message from panel */
void gpu_update_display (GPUPlugin *g)
{
    GdkRGBA none = {0, 0, 0, 0};
    graph_reload (&(g->graph), g->icon_size, g->background_colour, g->foreground_colour, none, none);
}

void gpu_init (GPUPlugin *g)
{
    /* Allocate icon as a child of top level */
    graph_init (&(g->graph));
    gtk_container_add (GTK_CONTAINER (g->plugin), g->graph.da);

    gpu_update_display (g);

    /* Set up long press */
    g->gesture = add_long_press (g->plugin, NULL, NULL);

    /* Connect a timer to refresh the statistics. */
    g->timer = g_timeout_add (1500, (GSourceFunc) gpu_update, (gpointer) g);

    /* Show the widget and return. */
    gtk_widget_show_all (g->plugin);
}

void gpu_destructor (gpointer user_data)
{
    GPUPlugin *g = (GPUPlugin *) user_data;
    graph_free (&(g->graph));
    if (g->timer) g_source_remove (g->timer);
    if (g->gesture) g_object_unref (g->gesture);
    g_free (g);
}

/* End of file */
/*----------------------------------------------------------------------------*/
