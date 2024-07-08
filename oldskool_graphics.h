#ifndef OLDSKOOL_GRAPHICS_H
#define OLDSKOOL_GRAPHICS_H
#include "vector_math.h"
#include "volk.h"

typedef struct VGWindow VGWindow;

typedef struct OldskoolContext OldskoolContext;
enum { OS_IDLE, OS_POINTS, OS_LINES, OS_TRIANGLES };

OldskoolContext * osCreate(VGWindow * wind);
void osDestroy(OldskoolContext * k);

void osReset(OldskoolContext * k);

void osClearColor(OldskoolContext * k, vec4 color);

void osLoadMatrix(OldskoolContext * k, mat4 m);
void osPushMatrix(OldskoolContext * k, mat4 m);
void osPopMatrix(OldskoolContext * k);

void osBegin(OldskoolContext * k, int primtype);
void osEnd(OldskoolContext * k);

void osSubmit(OldskoolContext * k, VkCommandBuffer cmdbuf, bool parity);

void osVertex4(OldskoolContext * k, vec4 v);
void osVertex3(OldskoolContext * k, vec3 v);
void osVertex2(OldskoolContext * k, vec2 v);

void osColor4(OldskoolContext * k, vec4 v);
void osColor3(OldskoolContext * k, vec3 v);

enum { OS_FLOAT, OS_UNSIGNED_BYTE, OS_UNSIGNED_SHORT, OS_UNSIGNED_INT, OS_BYTE, OS_SHORT, OS_INT };

int osVertexPointer(OldskoolContext * k, int vector_width, int type, int count, int stride, void * data, int offset);
int osColorPointer(OldskoolContext * k, int vector_width, int type, int count, int stride, void * data, int offset);

int osDrawArrays(OldskoolContext * k, int mode, int start, int count);
int osDrawElements(OldskoolContext * k, int mode, int count, int type, void * indices);

#endif
