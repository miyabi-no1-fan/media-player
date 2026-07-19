#pragma once
#include "slp_image.h"

#ifdef __cplusplus
extern "C" {
#endif

slp_image_t slp_png_read(const char* path);

// only call free(image->buffer); and set image to 0
void slp_image_destroy(slp_image_t* image);

/*
not support interlace and color type 3 imsave
return:
3 => deflate fail
2 => wrong input
1 => file write failure
-1 => malloc fail
0 => success
*/
int slp_png_write(slp_image_t image, const char* path);

#ifdef __cplusplus
}
#endif