# Vanity Plot

A little repl-oriented plotting visualizer. It's useful for visualizing data while designing or debugging algorithms in a repl. The library is in C, with Vanity Scheme bindings.

Example Usage in C:

```
int main() {
  Plot * myplot = make_plot(600, 600);
  float xs[] = { 1, 2, 3, 4, 5, };
  float ys[] = { 1, 4, 9, 16, 25, };
  int ct = sizeof xs / sizeof *xs;
  plot_line_strip(myplot, ct, xs, ys);
  plot_color(myplot, 1, 0, 0);
  plot_points(myplot, ct, xs, ys);
  while(plot_alive(myplot))
    ;
  close_plot(myplot);
}
```

Example Usage in Vanity Scheme:

```
(import (vanity plot))
(define myplot (make-plot 600 600))
(define xs #f32(1 2 3 4 5))
(define ys #f32(1 4 9 16 25))
(plot-line-strip myplot xs ys)
(plot_color myplot 1 0 0)
(plot-points myplot xs ys)
(let loop () (if (plot-alive? myplot) (loop)))
```
