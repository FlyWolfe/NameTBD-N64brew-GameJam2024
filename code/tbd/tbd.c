#include <libdragon.h>
#include "../../core.h"
#include "../../minigame.h"
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3ddebug.h>

#define FONT_TEXT           1
#define FONT_BILLBOARD      2
#define TEXT_COLOR          0x6CBB3CFF
#define TEXT_OUTLINE        0x30521AFF
#define DEBUG_COLOR         0xFFFFFFFF
#define DEBUG_OUTLINE       0x222222FF

// Hook/callback to modify tile settings set by t3d_model_draw
void tile_scroll(void* userData, rdpq_texparms_t *tileParams, rdpq_tile_t tile) {
  //if(tile == TILE1) {
    float offset = *(float*)userData;
    tileParams->s.translate = offset * 0.5f;
    tileParams->t.translate = offset * 0.8f;

    tileParams->s.translate = fm_fmodf(tileParams->s.translate, 32.0f);
    tileParams->t.translate = fm_fmodf(tileParams->t.translate, 32.0f);
  //}
}

typedef struct {
  T3DVec3 pos;
  float strength;
  color_t color;
} PointLight;

surface_t *depthBuffer;
T3DViewport viewport;
rdpq_font_t *font;
rdpq_font_t *fontBillboard;
T3DMat4FP* waterMatFP;
rspq_block_t *dplWater;
T3DModel *waterModel;
T3DVec3 camPos;
T3DVec3 camTarget;
T3DVec3 lightDirVec;
xm64player_t music;
float tileOffset;
float transformOffset;

rspq_syncpoint_t syncPoint;

const MinigameDef minigame_def = {
  .gamename = "Bumper Boats",
  .developername = "FlyWolfe",
  .description = "Happily fight at the ends of the ocean and succeed or be doooooomed.",
  .instructions = "Joystick to move. Bump the other boats into the pit of despair."
};

/*==============================
    minigame_init
    The minigame initialization function
==============================*/
void minigame_init()
{
  const color_t colors[] = {
    color_from_packed32(PLAYERCOLOR_1<<8),
    color_from_packed32(PLAYERCOLOR_2<<8),
    color_from_packed32(PLAYERCOLOR_3<<8),
    color_from_packed32(PLAYERCOLOR_4<<8),
  };
  
  display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
  depthBuffer = display_get_zbuf();

  t3d_init((T3DInitParams){});

  font = rdpq_font_load("rom:/squarewave.font64");
  rdpq_text_register_font(FONT_TEXT, font);
  rdpq_font_style(font, 0, &(rdpq_fontstyle_t){.color = color_from_packed32(DEBUG_COLOR) });
  
  fontBillboard = rdpq_font_load("rom:/squarewave.font64");
  rdpq_text_register_font(FONT_BILLBOARD, fontBillboard);
  for (size_t i = 0; i < MAXPLAYERS; i++)
  {
    rdpq_font_style(fontBillboard, i, &(rdpq_fontstyle_t){ .color = colors[i] });
  }
  
  viewport = t3d_viewport_create();
  
  T3DMat4 waterMat; // matrix for our model, this is a "normal" float matrix
  t3d_mat4_identity(&waterMat);
  t3d_mat4_scale(&waterMat, 0.12f, 0.12f, 0.12f);

  waterMatFP = malloc_uncached(sizeof(T3DMat4FP));
  t3d_mat4_to_fixed(waterMatFP, &waterMat);

  camPos = (T3DVec3){{0, 75.0f, 100.0f}};
  camTarget = (T3DVec3){{0, 0, 40}};

  lightDirVec = (T3DVec3){{-1.0f, 1.0f, 1.0f}};
  t3d_vec3_norm(&lightDirVec);
  
  waterModel = t3d_model_load("rom:/tbd/water.t3dm");
  tileOffset = 0.0f;
  transformOffset = 0.0f;
  
  rspq_block_begin();
    t3d_matrix_push(waterMatFP);
    t3d_model_draw_custom(waterModel, (T3DModelDrawConf){
      .userData = &tileOffset,
      .tileCb = tile_scroll,
    });
    t3d_matrix_pop(1);
  dplWater = rspq_block_end();
  
  syncPoint = 0;
  //wav64_open(&sfx_start, "rom:/core/Start.wav64");
  //wav64_open(&sfx_countdown, "rom:/core/Countdown.wav64");
  //wav64_open(&sfx_stop, "rom:/core/Stop.wav64");
  //wav64_open(&sfx_winner, "rom:/core/Winner.wav64");
  xm64player_open(&music, "rom:/snake3d/bottled_bubbles.xm64");
  xm64player_play(&music, 0);
}

/*==============================
    minigame_fixedloop
    Code that is called every loop, at a fixed delta time.
    Use this function for stuff where a fixed delta time is 
    important, like physics.
    @param  The fixed delta time for this tick
==============================*/
void minigame_fixedloop(float deltaTime)
{

}

/*==============================
    minigame_loop
    Code that is called every loop.
    @param  The delta time for this tick
==============================*/
void minigame_loop(float deltaTime)
{
  uint8_t colorAmbient[4] = {0x22, 0x11, 0x22, 0xFF};
  uint8_t colorDir[4] = {0x88, 0x88, 0x88, 0xFF};

  tileOffset += 5.0f * deltaTime;
	transformOffset += 0.2f * deltaTime;
  
  t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(90.0f), 20.0f, 160.0f);
  t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});
  
  // ======== Transform Water Mesh ======== //
  // returns the global vertex buffer for a model.
  // If you have multiple models and want to only update one, you have to manually iterate over the objects.
  // see the implementation of t3d_model_draw_custom in that case.
  T3DVertPacked* verts = t3d_model_get_vertices(waterModel);
  float globalHeight = fm_sinf(transformOffset * 2.5f) * 30.0f;

  for(uint16_t i=0; i < waterModel->totalVertCount; ++i)
  {
    // To better handle the interleaved vertex format,
    // t3d provides a few helper functions to access attributes
    int16_t *pos = t3d_vertbuffer_get_pos(verts, i);

    // water-like wobble effect
    float height = fm_sinf(
      transformOffset * 4.5f
      + pos[0] * 300.1f
      + pos[2] * 200.1f
    );
    pos[1] = 50.0f * height + globalHeight;

    // make lower parts darker, and higher parts brighter
    float color = height * 0.25f + 0.75f;
    uint8_t* rgba = t3d_vertbuffer_get_rgba(verts, i);
    rgba[0] = color * 255;
    rgba[1] = color * 255;
    rgba[2] = color * 255;
    rgba[3] = 0xFF;
  }

  // Flush the cache again
  data_cache_hit_writeback(verts, sizeof(T3DVertPacked) * waterModel->totalVertCount / 2);
  
  // ======== Draw (3D) ======== //
  rdpq_attach(display_get(), depthBuffer);
  t3d_frame_start();
  t3d_viewport_attach(&viewport);
  
  rdpq_mode_fog(RDPQ_FOG_STANDARD);
  rdpq_set_fog_color((color_t){20, 80, 140, 0xFF});

  t3d_screen_clear_color(RGBA32(0x22, 0x55, 0x66, 0xFF));
  t3d_screen_clear_depth();
  
  t3d_light_set_ambient(colorAmbient);
  t3d_light_set_directional(0, colorDir, &lightDirVec);
  t3d_light_set_count(1);
  
  // Draw water
  rspq_block_run(dplWater);
  /*t3d_matrix_push(waterMatFP);
  t3d_model_draw_custom(waterModel, (T3DModelDrawConf){
    .userData = &tileOffset,
    .tileCb = tile_scroll,
  });
  t3d_matrix_pop(1);
  */
  t3d_fog_set_range(0.4f, 120.0f);
  t3d_fog_set_enabled(true);
  
  syncPoint = rspq_syncpoint_new();
  
  rdpq_sync_tile();
  rdpq_sync_pipe(); // Hardware crashes otherwise
  
  // ======== DEBUG ======== //
  rdpq_text_printf(NULL, FONT_TEXT, 16, 20, "FPS: %.4f\n", display_get_fps());
  
  // ======== Draw (2D) ======== //
  
  rdpq_detach_show();
}

/*==============================
    minigame_cleanup
    Clean up any memory used by your game just before it ends.
==============================*/
void minigame_cleanup()
{

}