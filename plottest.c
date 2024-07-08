#include "plot.h"

int main() {
  Plot * p = make_plot(800, 600);

  plot_line(p, 0, 1, 0, 25);

  /*
  plot_line(p, 1, 1, 2, 4);
  plot_line(p, 2, 4, 3, 9);
  plot_line(p, 3, 9, 4, 16);
  plot_line(p, 4, 16, 5, 25);
  */
  float xs[] = { 1, 2, 3, 4, 5 };
  float ys[] = { 1, 4, 9, 16, 25 };
  plot_line_strip(p, sizeof xs / sizeof *xs, xs, ys);

  plot_color(p, 1, 0, 0);

  plot_line(p, 0, 0, 5, 0);
  plot_line(p, 5, -1, 0, -1);

  plot_points(p, sizeof xs / sizeof *xs, xs, ys);


  while(plot_alive(p)) {
    __builtin_ia32_pause();
  }
  close_plot(p);
}
