#pragma once
#include <stdbool.h>

typedef struct Plot Plot;
Plot * make_plot(int w, int h);
bool plot_alive(Plot * plot);
void close_plot(Plot * plot);

void plot_color(Plot * plot, float r, float g, float b);

void plot_point(Plot * plot, float x, float y);
void plot_points(Plot * plot, int ct, float * xs, float * ys);

void plot_line(Plot * plot, float x1, float y1, float x2, float y2);
void plot_line_strip(Plot * plot, int ct, float * xs, float * ys);

//void plot_bitmap_rgba8(Plot * plot, float x1, float y1, float x2, float y2, int w, int h, bool nearest, const unsigned char * bits);

void plot_continuous(Plot * plot);
void plot_clear(Plot * plot);
void plot_begin_frame(Plot * plot);
void plot_end_frame(Plot * plot);
