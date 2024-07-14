#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "vector_math.h"
#include "volk.h"
#include "vanity_graphics_private.h"
#include "oldskool_graphics.h"

#include "plot.h"

// goal here is enough to do visualization in a repl, R style
// need to be able to: draw points, lines
// color it
// have the window run async

// call it in scheme

struct Plot {
  bool alive;
  int pipe;
  int child;
};

static sig_atomic_t plot_running;
static void handle_sigterm(int signal) {
  plot_running = 0;
}

typedef struct WindStatus {
  bool program_exit;
  bool minimized;
  bool needs_resize;
} WindStatus;

static void poll_events(VGWindow * wind, WindStatus * status) {
  SDL_Event windowEvent;

  while(SDL_PollEvent(&windowEvent)) {
    if(windowEvent.type == SDL_QUIT) {
      status->program_exit = true;
      break;
    }
    if(windowEvent.type == SDL_WINDOWEVENT) {
      switch(windowEvent.window.event) {
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_MAXIMIZED:
          status->needs_resize = true;
          break;
        case SDL_WINDOWEVENT_MINIMIZED:
          status->minimized = true;
          break;
        case SDL_WINDOWEVENT_RESTORED:
          status->minimized = false;
          break;
      }
    }
  }
}

static void child_loop(VGWindow * win, int pipe);
Plot * make_plot(int w, int h) {

  int fds[2];
  assert(pipe(fds) == 0);
  int read_end = fds[0];
  int write_end = fds[1];
  assert(fcntl(read_end, F_SETFL, O_NONBLOCK) == 0);
  
  int child = fork();

  if(child) {
    // parent process
    close(read_end);
    signal(SIGPIPE, SIG_IGN);
    Plot * plot = malloc(sizeof(Plot));
    plot->alive = true;
    plot->child = child;
    plot->pipe = write_end;
    return plot;
  } else {
    // child process
    close(write_end);
    signal(SIGINT, SIG_IGN);
    plot_running = 1;
    signal(SIGTERM, handle_sigterm);

    bool ok = true;
    ok &= !VG_Init();
    VGWindow * wind;
    ok &= !!(wind = VG_CreateWindow(w, h));
    ok &= !VG_CreateAppObjects(wind);
    ok &= !VG_CreateSwapchain(wind);
    if(!ok) {
      printf("failed to make window\n");
      exit(1);
    }

    child_loop(wind, read_end);
  }

  return NULL;
}

bool plot_alive(Plot * p) {
  if(!p->alive)
    return false;
  return !waitpid(p->child, NULL, WNOHANG);

}

void close_plot(Plot * plot) {
  close(plot->pipe);
  kill(plot->child, SIGTERM);
  pid_t child;
  while((child = waitpid(plot->child, NULL, 0)) != -1 || errno == EINTR) {
    if(errno == EINTR)
    {
      errno = 0;
    }
  }
}

typedef struct Point {
  float x;
  float y;
} Point;
typedef struct Geometry {
  int ct;
  float * xs;
  float * ys;
} Geometry;
typedef struct Line {
  float x1;
  float y1;
  float x2;
  float y2;
} Line;
typedef struct Bitmap {
  float x1;
  float y1;
  float x2;
  float y2;

  bool nearest : 1;

  int w;
  int h;
  int num_channels;
  //GLuint tex;
} Bitmap;

enum plot_cmd_t { NULL_COMMAND, PLOT_POINT, PLOT_POINTS, PLOT_LINE, PLOT_LINES, PLOT_COLOR, PLOT_BITMAP, PLOT_CONTINUOUS, PLOT_CLEAR, PLOT_BEGIN_FRAME, PLOT_END_FRAME }; 
typedef struct PlotCommand {
  enum plot_cmd_t type;
  union {
    Point point;
    Line line;
    Geometry geos;
    Bitmap bitmap;
    vec3 color;
  };
} PlotCommand;
static_assert(sizeof(PlotCommand) < PIPE_BUF);

static void write_cmd(PlotCommand * cmd, Plot * plot) {
  int pipe = plot->pipe;
  ssize_t ret = write(pipe, cmd, sizeof *cmd);
  while(ret == -1) {
    if(errno == EAGAIN || errno == EINTR) {
      errno = 0;
    } else if(errno == EPIPE) {
      plot->alive = false;
      break;
    } else {
      assert(0);
    }
  }
}

static void write_big_data(char * bits, size_t size, Plot * plot) {
  int pipe = plot->pipe;
  ssize_t ret;
  while((ret = write(pipe, bits, size)) != size) {
    if(errno == EAGAIN || errno == EINTR) {
      errno = 0;
      continue;
    } else if(errno == EPIPE) {
      plot->alive = false;
      break;
    } else if(ret == -1) {
      assert(0);
    } else {
      printf("wrote %ld\n", ret);
      bits += ret;
      size -= ret;
    }
  }
}

static char * read_big_data(size_t size, int pipe) {
  char * ret = malloc(size);
  char * cur = ret;
  int nloops = 0;
  ssize_t read_size;
  while((read_size = read(pipe, cur, size)) != size) {
    if(read_size == -1) {
      assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
      continue;
    } else if(read_size == 0) {
      // pipe closed
      plot_running = 0;
      return ret;
    } else {
      nloops += 1;
      cur += read_size;
      size -= read_size;
    }
  }
  return ret;
}

void plot_color(Plot * plot, float r, float g, float b) {
  PlotCommand cmd = {
    .type = PLOT_COLOR,
    .color = make_vec3(r, g, b),
  };
  write_cmd(&cmd, plot);
}

void plot_point(Plot * plot, float x, float y) {
  PlotCommand cmd = {
    .type = PLOT_POINT,
    .point = {
      .x = x,
      .y = y
    }
  };
  write_cmd(&cmd, plot);
}

void plot_line(Plot * plot, float x1, float y1, float x2, float y2) {
  PlotCommand cmd = {
    .type = PLOT_LINE,
    .line = {
      .x1 = x1,
      .y1 = y1,
      .x2 = x2,
      .y2 = y2,
    }
  };
  write_cmd(&cmd, plot);
}

void plot_points(Plot * plot, int ct, float * xs, float * ys) {
  PlotCommand cmd = {
    .type = PLOT_POINTS,
    .geos = {
      .ct = ct,
    },
  };
  write_cmd(&cmd, plot);
  write_big_data((char*)xs, sizeof(float[ct]), plot);
  write_big_data((char*)ys, sizeof(float[ct]), plot);
}
void plot_line_strip(Plot * plot, int ct, float * xs, float * ys) {
  PlotCommand cmd = {
    .type = PLOT_LINES,
    .geos = {
      .ct = ct,
    },
  };
  write_cmd(&cmd, plot);
  write_big_data((char*)xs, sizeof(float[ct]), plot);
  write_big_data((char*)ys, sizeof(float[ct]), plot);
}

void plot_bitmap_rgba8(Plot * plot, float x1, float y1, float x2, float y2, int w, int h, bool nearest, const unsigned char * bits) {
  PlotCommand cmd = {
    .type = PLOT_BITMAP,
    .bitmap = {
      .x1 = x1,
      .y1 = y1,
      .x2 = x2,
      .y2 = y2,

      .nearest = nearest,

      .w = w,
      .h = h,
      .num_channels = 4,
      //.tex = 0
    }
  };
  write_cmd(&cmd, plot);
  size_t size = 4 * w * h;
  write_big_data((char*)bits, size, plot);
}

void plot_continuous(Plot * plot) {
  PlotCommand cmd = {
    .type = PLOT_CONTINUOUS,
  };
  write_cmd(&cmd, plot);
}
void plot_clear(Plot * plot) {
  PlotCommand cmd = {
    .type = PLOT_CLEAR,
  };
  write_cmd(&cmd, plot);
}
void plot_begin_frame(Plot * plot) {
  PlotCommand cmd = {
    .type = PLOT_BEGIN_FRAME,
  };
  write_cmd(&cmd, plot);
}
void plot_end_frame(Plot * plot) {
  PlotCommand cmd = {
    .type = PLOT_END_FRAME,
  };
  write_cmd(&cmd, plot);
}

static size_t num_cmds = 0;
static size_t len_cmds = 0;
static PlotCommand * cmds = NULL;

/*
static GLuint load_bitmap(PlotCommand cmd, int pipe) {
  assert(cmd.type == PLOT_BITMAP);

  size_t size = 4 * cmd.bitmap.w * cmd.bitmap.h;
  char * bits = read_big_data(size, pipe);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cmd.bitmap.w, cmd.bitmap.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bits);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  if(cmd.bitmap.nearest)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  free(bits);
  return tex;
}
*/

static void wipe_cmds() {
  for(int i = 0; i < num_cmds; i++) {
    switch(cmds[i].type) {
      case PLOT_POINTS:
      case PLOT_LINES:
        free(cmds[i].geos.xs);
        free(cmds[i].geos.ys);
        break;
      case PLOT_BITMAP:
        assert(0);
        break;
      default:
        break;
    }
  }
  num_cmds = 0;
}

static bool continuous_draw = true;

static void read_cmds(VGWindow * wind, WindStatus * status, int pipe) {
  PlotCommand cmd;
  bool done = false;
  while(!done) {
    ssize_t ret = read(pipe, &cmd, sizeof cmd);
    if(ret == -1) {
      assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
      if(continuous_draw)
      {
        return;
      }
      else
      {
        // need to pump the event queue when we're waiting on commands
        // very hacky but should work
        poll_events(wind, status);
        if(status->program_exit) {
          plot_running = 0;
          return;
        }
        usleep(1);
        continue;
      }
    } else if(ret == 0) {
      // eof means plot closed, time to die
      plot_running = 0;
      return;
    }
    assert(ret == sizeof cmd);

    if(!cmds) {
      len_cmds = 16;
      cmds = malloc(sizeof(PlotCommand[len_cmds]));
    }
    if(num_cmds >= len_cmds) {
      len_cmds = 2*num_cmds;
      cmds = realloc(cmds, sizeof(PlotCommand[len_cmds]));
    }

    switch(cmd.type) {
      case PLOT_CONTINUOUS:
        if(!continuous_draw)
          wipe_cmds();
        continuous_draw = true;
        break;
      case PLOT_CLEAR:
        wipe_cmds();
        break;
      case PLOT_BEGIN_FRAME:
        wipe_cmds();
        continuous_draw = false;
        break;
      case PLOT_END_FRAME:
        done = true;
        break;

      case PLOT_POINTS:
      case PLOT_LINES:
        cmd.geos.xs = (float*)read_big_data(sizeof(float[cmd.geos.ct]), pipe);
        cmd.geos.ys = (float*)read_big_data(sizeof(float[cmd.geos.ct]), pipe);
        cmds[num_cmds++] = cmd;
        break;
      case PLOT_BITMAP:
        //cmd.bitmap.tex = load_bitmap(cmd, pipe);
        assert(0);
        cmds[num_cmds++] = cmd;
        break;
      default:
        cmds[num_cmds++] = cmd;
        break;
    }

  }
}

static float min(float a, float b) {
  return a < b ? a : b;
}

static float max(float a, float b) {
  return a >= b ? a : b;
}

static void draw_cmds(VGWindow * win, OldskoolContext * osk) {
  float minx = INFINITY, miny = INFINITY;
  float maxx = -INFINITY, maxy = -INFINITY;

  for(size_t i = 0; i < num_cmds; i++) {
    PlotCommand cmd = cmds[i];
    switch(cmd.type) {
      case PLOT_POINT:
        minx = min(minx, cmd.point.x);
        miny = min(miny, cmd.point.y);
        maxx = max(maxx, cmd.point.x);
        maxy = max(maxy, cmd.point.y);
        break;
      case PLOT_POINTS:
      case PLOT_LINES:
        for(int i = 0; i < cmd.geos.ct; i++) {
          minx = min(minx, cmd.geos.xs[i]);
          miny = min(miny, cmd.geos.ys[i]);
          maxx = max(maxx, cmd.geos.xs[i]);
          maxy = max(maxy, cmd.geos.ys[i]);
        }
        break;
      case PLOT_LINE:
        minx = min(minx, cmd.line.x1);
        miny = min(miny, cmd.line.y1);
        maxx = max(maxx, cmd.line.x1);
        maxy = max(maxy, cmd.line.y1);

        minx = min(minx, cmd.line.x2);
        miny = min(miny, cmd.line.y2);
        maxx = max(maxx, cmd.line.x2);
        maxy = max(maxy, cmd.line.y2);
        break;
      case PLOT_BITMAP:
        minx = min(minx, cmd.bitmap.x1);
        miny = min(miny, cmd.bitmap.y1);
        maxx = max(maxx, cmd.bitmap.x1);
        maxy = max(maxy, cmd.bitmap.y1);

        minx = min(minx, cmd.bitmap.x2);
        miny = min(miny, cmd.bitmap.y2);
        maxx = max(maxx, cmd.bitmap.x2);
        maxy = max(maxy, cmd.bitmap.y2);
        break;
      default:
        break;
    }
  }
  // no data to draw
  if(minx > maxy)
    return;
  if(minx == maxx) {
    minx -= 1;
    maxx += 1;
  }
  if(miny == maxy) {
    miny -= 1;
    maxy += 1;
  }
  float spanx = maxx - minx;
  float spany = maxy - miny;

  minx -= spanx * 0.05;
  maxx += spanx * 0.05;

  miny -= spany * 0.05;
  maxy += spany * 0.05;

  float aspect = win->swap_extent.width / (float) win->swap_extent.height;

  float psizex = 4.0 / win->swap_extent.width;
  float psizey = 4.0 / win->swap_extent.height;

  float lsizex = 1.0 / win->swap_extent.width;
  float lsizey = 1.0 / win->swap_extent.height;

  // the telltale matrix that the author has opengl brain damage
  mat4 vulkan_squish = { {
    make_vec4(1, 0, 0, 0),
    make_vec4(0,-1, 0, 0),
    make_vec4(0, 0,.5,.5),
    make_vec4(0, 0, 0, 1),
  } };
  osLoadMatrix(osk, vulkan_squish);
  mat4 mat = mat4_ortho(minx, maxx, miny, maxy, 1, -1);

  vec4 p0 = make_vec4(-psizex, -psizey, 0, 0);
  vec4 p1 = make_vec4(+psizex, -psizey, 0, 0);
  vec4 p2 = make_vec4(+psizex, +psizey, 0, 0);
  vec4 p3 = make_vec4(-psizex, +psizey, 0, 0);

  vec3 color = make_vec3(0);
  osBegin(osk, OS_TRIANGLES);
  for(size_t i = 0; i < num_cmds; i++) {
    PlotCommand cmd = cmds[i];
    switch(cmd.type) {
      case PLOT_COLOR:
        color = cmd.color;
        break;
      case PLOT_POINT:
        osColor3(osk, color);
        vec4 pt = mat4_mul_vec4(mat, make_vec4(cmd.point.x, cmd.point.y, 0, 1));
          osVertex4(osk, vec4_add(p0, pt));
          osVertex4(osk, vec4_add(p1, pt));
          osVertex4(osk, vec4_add(p2, pt));

          osVertex4(osk, vec4_add(p0, pt));
          osVertex4(osk, vec4_add(p2, pt));
          osVertex4(osk, vec4_add(p3, pt));
        break;
      case PLOT_POINTS:
        osColor3(osk, color);
        for(int i = 0; i < cmd.geos.ct; i++) {
          vec4 pt = mat4_mul_vec4(mat, make_vec4(cmd.geos.xs[i], cmd.geos.ys[i], 0, 1));
          osVertex4(osk, vec4_add(p0, pt));
          osVertex4(osk, vec4_add(p1, pt));
          osVertex4(osk, vec4_add(p2, pt));

          osVertex4(osk, vec4_add(p0, pt));
          osVertex4(osk, vec4_add(p2, pt));
          osVertex4(osk, vec4_add(p3, pt));
        }
        break;
      case PLOT_LINE:
      {
        Line line = cmd.line;
        if(line.x1 == line.x2 && line.y1 == line.y2)
          continue;
        vec4 s0 = mat4_mul_vec4(mat, make_vec4(line.x1, line.y1, 0, 1));
        vec4 s1 = mat4_mul_vec4(mat, make_vec4(line.x2, line.y2, 0, 1));

        vec2 tangent = vec2_normalize(make_vec2(vec4_getX(s1) - vec4_getX(s0), (vec4_getY(s1) - vec4_getY(s0))/aspect));
        vec2 normal = make_vec2(-vec2_getY(tangent), vec2_getX(tangent));

        tangent = make_vec2(lsizex * vec2_getX(tangent), lsizey * vec2_getY(tangent));
        normal = make_vec2(lsizex * vec2_getX(normal), lsizey * vec2_getY(normal));

        vec4 p0 = make_vec4(vec2_scale(-1, normal), 0, 0); // s0
        vec4 p1 = make_vec4(vec2_scale(-1, normal), 0, 0); // s1
        vec4 p2 = make_vec4(vec2_scale(+1, normal), 0, 0); // s1
        vec4 p3 = make_vec4(vec2_scale(+1, normal), 0, 0); // s0

        p0 = vec4_add(p0, s0);
        p1 = vec4_add(p1, s1);
        p2 = vec4_add(p2, s1);
        p3 = vec4_add(p3, s0);

        osColor3(osk, color);
          osVertex4(osk, p0);
          osVertex4(osk, p1);
          osVertex4(osk, p2);

          osVertex4(osk, p0);
          osVertex4(osk, p2);
          osVertex4(osk, p3);
        break;
      }
      case PLOT_LINES:
      {
        osColor3(osk, color);
        vec4 last_p1;
        vec4 last_p2;
        for(int i = 1; i < cmd.geos.ct; i++) {
          vec4 s0 = mat4_mul_vec4(mat, make_vec4(cmd.geos.xs[i-1], cmd.geos.ys[i-1], 0, 1));
          vec4 s1 = mat4_mul_vec4(mat, make_vec4(cmd.geos.xs[i-0], cmd.geos.ys[i-0], 0, 1));

          vec2 tangent = vec2_normalize(make_vec2(vec4_getX(s1) - vec4_getX(s0), (vec4_getY(s1) - vec4_getY(s0))/aspect));
          vec2 normal = make_vec2(-vec2_getY(tangent), vec2_getX(tangent));

          tangent = make_vec2(lsizex * vec2_getX(tangent), lsizey * vec2_getY(tangent));
          normal = make_vec2(lsizex * vec2_getX(normal), lsizey * vec2_getY(normal));

          vec4 p0 = make_vec4(vec2_scale(-1, normal), 0, 0); // s0
          vec4 p1 = make_vec4(vec2_scale(-1, normal), 0, 0); // s1
          vec4 p2 = make_vec4(vec2_scale(+1, normal), 0, 0); // s1
          vec4 p3 = make_vec4(vec2_scale(+1, normal), 0, 0); // s0

          p0 = vec4_add(p0, s0);
          p1 = vec4_add(p1, s1);
          p2 = vec4_add(p2, s1);
          p3 = vec4_add(p3, s0);
          if(i != 1) {
            // draw connection geometry
            osVertex4(osk, p0);
            osVertex4(osk, last_p2);
            osVertex4(osk, last_p1);

            osVertex4(osk, p3);
            osVertex4(osk, last_p2);
            osVertex4(osk, last_p1);
          }
          osVertex4(osk, p0);
          osVertex4(osk, p1);
          osVertex4(osk, p2);

          osVertex4(osk, p0);
          osVertex4(osk, p2);
          osVertex4(osk, p3);

          last_p1 = p1;
          last_p2 = p2;
        }
      }
      /*
      case PLOT_BITMAP:
      {
        Bitmap bit = cmd.bitmap;
        float minx = min(bit.x1, bit.x2);
        float maxx = max(bit.x1, bit.x2);
        float miny = min(bit.y1, bit.y2);
        float maxy = max(bit.y1, bit.y2);
        vec2 p0 = make_vec2(minx, miny);
        vec2 p1 = make_vec2(maxx, miny);
        vec2 p2 = make_vec2(maxx, maxy);
        vec2 p3 = make_vec2(minx, maxy);

        vec2 t0 = make_vec2(0, 0);
        vec2 t1 = make_vec2(1, 0);
        vec2 t2 = make_vec2(1, 1);
        vec2 t3 = make_vec2(0, 1);

        //vi3d_end();
        vi3d_texture2d(cmd.bitmap.tex, 0);
        vi3d_begin(VI3D_TRIANGLES);
          vi3d_color3f(1, 1, 1);
          vi3d_texcoord2(t0);
          vi3d_vertex2(p0);
          vi3d_texcoord2(t1);
          vi3d_vertex2(p1);
          vi3d_texcoord2(t2);
          vi3d_vertex2(p2);

          vi3d_texcoord2(t2);
          vi3d_vertex2(p2);
          vi3d_texcoord2(t3);
          vi3d_vertex2(p3);
          vi3d_texcoord2(t0);
          vi3d_vertex2(p0);
        vi3d_end();
        vi3d_texture2d(0, 0);
      }
      */
      default:
        break;
    }
  }
  osEnd(osk);
}

static void child_loop(VGWindow * wind, int pipe) {
  WindStatus status = {0};
  bool frame_parity = false;
  
  VkCommandBuffer command_buf[2];
  {
    VkCommandBufferAllocateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = wind->commandpool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 2,
    };
    if(vkAllocateCommandBuffers(wind->device, &createInfo, command_buf) != VK_SUCCESS) {
      fprintf(stderr, "failed to create command pool\n");
      exit(1);
    }
  }
  OldskoolContext * osk = osCreate(wind);
  
  while(plot_running) {
    poll_events(wind, &status);
    if(status.program_exit) {
      plot_running = 0;
      break;
    }

    read_cmds(wind, &status, pipe);

    if(status.minimized)
      continue;

    if(!wind->swapchain_created)
    {
      int w,h;
      SDL_Vulkan_GetDrawableSize(wind->window, &w, &h);
      bool valid_window = w != 0 && h != 0;
      if(!valid_window) {
        VG_RecreateSwapchain(wind);
      }
      if(!wind->swapchain_created) {
        continue;
      }
    }

    vkWaitForFences(wind->device, 1, &wind->frame_fence[frame_parity], VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    {
      VkResult ret = vkAcquireNextImageKHR(wind->device, wind->swapchain, UINT64_MAX, wind->image_available[frame_parity], VK_NULL_HANDLE, &image_index);
      if(ret == VK_ERROR_OUT_OF_DATE_KHR) {
        if(VG_RecreateSwapchain(wind)) {
          fprintf(stderr, "failed to resize window\n");
          exit(1);
        }
        continue;

      } else if(ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "failed to acquire swapchain image\n");
        exit(1);
      }
    }

    vkResetFences(wind->device, 1, &wind->frame_fence[frame_parity]);

    {
      VkCommandBufferBeginInfo beginfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = NULL,
      };
      if(vkBeginCommandBuffer(command_buf[frame_parity], &beginfo) != VK_SUCCESS) {
        fprintf(stderr, "failed to begin command recording\n");
        exit(1);
      }

      VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = wind->primary_frameimage.img,
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = 1,
          .layerCount = 1,
        },
      };

      vkCmdPipelineBarrier(command_buf[frame_parity],
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, NULL,
        0, NULL,
        1, &image_barrier
      );

      VkRenderPassBeginInfo rpinfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = wind->renderpass,
        .framebuffer = wind->primary_framebuffer,
        .renderArea.offset = { 0, 0 },
        .renderArea.extent = wind->swap_extent,
        .clearValueCount = 0,
        .pClearValues = NULL,
      };
      vkCmdBeginRenderPass(command_buf[frame_parity], &rpinfo, VK_SUBPASS_CONTENTS_INLINE);

      osReset(osk);
      osClearColor(osk, make_vec4(1));

      draw_cmds(wind, osk);

      osSubmit(osk, command_buf[frame_parity], frame_parity);

      vkCmdEndRenderPass(command_buf[frame_parity]);

      {
        VkImageMemoryBarrier image_barriers[] = {
          [0] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = wind->swapimages[image_index],
            .subresourceRange = {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
          },
          [1] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = wind->primary_frameimage.img,
            .subresourceRange = {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
          },
        };

        vkCmdPipelineBarrier(command_buf[frame_parity],
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0, NULL,
          0, NULL,
          2, image_barriers
        );

        VkImageResolve resolve_region = {
          .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .srcOffset = { 0, 0, 0 },
          .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .dstOffset = { 0, 0, 0 },
          .extent = { wind->swap_extent.width, wind->swap_extent.height, 1 },
        };

        vkCmdResolveImage(command_buf[frame_parity],
          wind->primary_frameimage.img,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          wind->swapimages[image_index],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1,
          &resolve_region);

        image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command_buf[frame_parity],
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          0,
          0, NULL,
          0, NULL,
          1, image_barriers
        );
      }

      if(vkEndCommandBuffer(command_buf[frame_parity]) != VK_SUCCESS) {
        fprintf(stderr, "failed to end command recording\n");
        exit(1);
      }
    }

    VkSemaphore waitSemaphores[] = { wind->image_available[frame_parity] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT };
    VkSemaphore signalSemaphores[] = { wind->render_finished[frame_parity] };
    VkSubmitInfo submitInfo = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = sizeof waitSemaphores / sizeof *waitSemaphores,
      .pWaitSemaphores = waitSemaphores,
      .pWaitDstStageMask = waitStages,

      .commandBufferCount = 1,
      .pCommandBuffers = &command_buf[frame_parity],

      .signalSemaphoreCount = sizeof signalSemaphores / sizeof *signalSemaphores,
      .pSignalSemaphores = signalSemaphores,
    };

    if(vkQueueSubmit(wind->graphics_queue, 1, &submitInfo, wind->frame_fence[frame_parity]) != VK_SUCCESS) {
      fprintf(stderr, "failed to submit command buffers\n");
      exit(1);
    }

    VkPresentInfoKHR presentInfo = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = signalSemaphores,

      .swapchainCount = 1,
      .pSwapchains = &wind->swapchain,
      .pImageIndices = &image_index,
      .pResults = NULL,
    };

    {
      VkResult ret = vkQueuePresentKHR(wind->present_queue, &presentInfo);
      if(status.needs_resize || ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
        VG_RecreateSwapchain(wind);
        status.needs_resize = false;
      } else if(ret != VK_SUCCESS) {
        fprintf(stderr, "failed to present frame\n");
        exit(1);
      }
    }
    frame_parity = !frame_parity;
  }

  close(pipe);
  
  VG_WaitIdle(wind);
  vkResetCommandBuffer(command_buf[0], 0);
  vkResetCommandBuffer(command_buf[1], 0);

  osDestroy(osk);
  VG_DestroyWindow(wind);
  VG_Quit();
  exit(0);
}
