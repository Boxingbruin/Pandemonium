#include "title_scene.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../../controllers/audio_controller.h"
#include "../../controllers/camera_controller.h"
#include "../../character/character.h"
#include "../../controllers/dialog_controller.h"
#include "../../utilities/button_prompt_utility.h"
#include "../../utilities/display_utility.h"
#include "../../utilities/game_lighting.h"
#include "../../utilities/general_utility.h"
#include "../../utilities/game_time.h"
#include "../../utilities/globals.h"
#include "../../utilities/joypad_utility.h"
#include "../../utilities/letterbox_utility.h"
#include "../../controllers/menu_controller.h"
#include "../../utilities/save_controller.h"
#include "../../utilities/video_layout.h"

#include "../../dev/dev.h"

#include "../../managers/cutscene_manager.h"

typedef enum {
    TITLE_STATE_INACTIVE = 0,
    TITLE_STATE_IDLE,
    TITLE_STATE_TRANSITION_TO_GUARDIAN,
    TITLE_STATE_DONE,
} TitleSceneState;

typedef enum {
    TITLE_SFX_WALK = 0,
    TITLE_SFX_COUNT,
} TitleSceneSfx;

static const char *TITLE_SFX_PATHS[TITLE_SFX_COUNT] = {
    [TITLE_SFX_WALK] = "rom:/audio/sfx/title_screen_walk_effect-22k.wav64",
};

static const float TITLE_ROOM_Y = -1.0f;
static const float TITLE_CHARACTER_YAW = T3D_PI * 0.5f;
static const float TITLE_CAMERA_FORWARD_SPEED = 15.0f;

static const float TITLE_TEXT_ACTIVATION_TIME = 50.0f;
static const float TITLE_START_GAME_TIME = 10.0f;
static const float TITLE_FADE_TIME = 7.0f;

static const char *TITLE_DIALOGS[] = {
    ">The Demon\nking has\nforced\nthe land\ninto a\ncentury long\ndarkness.",
    ">The King\nhas trained\na legion\nof powerful\nknights\nsworn to\nprotect the\nthrone.",
    ">These\nbattle born\nknights are\ntaken from\ntheir\nfamilies and\ncast into\nservitude.",
    ">Enduring\nblade and\ntorment\nuntil nothing\nremains but\nhollow armor."
};

#define TITLE_DIALOG_COUNT ((int)(sizeof(TITLE_DIALOGS) / sizeof(TITLE_DIALOGS[0])))

static TitleSceneState s_state = TITLE_STATE_INACTIVE;
static TitleSceneResult s_result = TITLE_SCENE_RESULT_NONE;

static bool s_screen_transition = false;
static bool s_screen_breath = false;
static bool s_skip_button_visible = false;
static bool s_last_cutscene_a_pressed = false;
static bool s_last_a_pressed = false;
static bool s_last_start_pressed = false;

static int s_current_title_dialog = 0;
static float s_title_text_activation_timer = 0.0f;
static float s_title_start_game_timer = 0.0f;

#define TITLE_ENVIRONMENT_PATH_PREFIX "rom:/boss_room/"

#define TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT 3
#define TITLE_ENVIRONMENT_FLOOR_MIP_CHAIN_COUNT 4
#define TITLE_ENVIRONMENT_WALLS_BACK_MIP_CHAIN_COUNT 3
#define TITLE_ENVIRONMENT_MAX_MIP_CHAIN_COUNT 4

#define TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL 2
#define TITLE_ENVIRONMENT_MIP2_START_RDP_LEVEL 4
#define TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT 8

_Static_assert(
    TITLE_ENVIRONMENT_FLOOR_MIP_CHAIN_COUNT
        <= TITLE_ENVIRONMENT_MAX_MIP_CHAIN_COUNT
        && TITLE_ENVIRONMENT_WALLS_BACK_MIP_CHAIN_COUNT
            <= TITLE_ENVIRONMENT_MAX_MIP_CHAIN_COUNT,
    "title environment mip binding storage is too small"
);

_Static_assert(
    TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT >= 3
        && TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT <= 8,
    "title environment mipmapping requires between 3 and 8 RDP levels"
);

typedef struct TitleEnvironmentMipChain {
    const char *chain_name;
    const char *material_name;
    uint8_t source_texture_count;
    const char *texture_paths[TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT];
    uint8_t source_start_rdp_levels[TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT];
    sprite_t *sprites[TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT];
    bool ready;
} TitleEnvironmentMipChain;

typedef struct TitleEnvironmentObject {
    const char *path;
    T3DModel *model;
    rspq_block_t *dpl;
    T3DMat4FP *matrix;
    bool draw_custom;
    bool frozen;
    bool optimized_mip_draw;
    ScrollParams *scroll_params;
    TitleEnvironmentMipChain *mip_chains;
    T3DMaterial *bound_mip_materials[TITLE_ENVIRONMENT_MAX_MIP_CHAIN_COUNT];
    uint8_t mip_chain_count;
} TitleEnvironmentObject;

static TitleEnvironmentMipChain s_title_floor_mip_chains[
    TITLE_ENVIRONMENT_FLOOR_MIP_CHAIN_COUNT
] = {
    {
        .chain_name = "floor6",
        .material_name = "floor",
        .source_texture_count = 3,
        .texture_paths = {
            NULL,
            TITLE_ENVIRONMENT_PATH_PREFIX "floor6-mip1.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "floor6-mip2.i4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL,
            TITLE_ENVIRONMENT_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chain_name = "floor_ornate11",
        .material_name = "floor_ornate",
        .source_texture_count = 3,
        .texture_paths = {
            NULL,
            TITLE_ENVIRONMENT_PATH_PREFIX "floor_ornate11-mip1.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "floor_ornate11-mip2.i4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL,
            TITLE_ENVIRONMENT_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chain_name = "floor_debris_pile5",
        .material_name = "floor_debris_pile2",
        .source_texture_count = 3,
        .texture_paths = {
            TITLE_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5-mip1.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5-mip2.i4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL,
            TITLE_ENVIRONMENT_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chain_name = "carpet_border8",
        .material_name = "carpet_border",
        .source_texture_count = 3,
        .texture_paths = {
            TITLE_ENVIRONMENT_PATH_PREFIX "carpet_border8.ci8.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "carpet_border8-mip1.ci8.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "carpet_border8-mip2.ci8.sprite",
        },
        .source_start_rdp_levels = {
            0,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL + 1,
        },
    },
};

static TitleEnvironmentMipChain s_title_walls_back_mip_chains[
    TITLE_ENVIRONMENT_WALLS_BACK_MIP_CHAIN_COUNT
] = {
    {
        .chain_name = "baseboard8",
        .material_name = "baseboard",
        .source_texture_count = 3,
        .texture_paths = {
            TITLE_ENVIRONMENT_PATH_PREFIX "baseboard8.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "baseboard8-mip1.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "baseboard8-mip2.i4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL,
            TITLE_ENVIRONMENT_MIP1_START_RDP_LEVEL + 1,
        },
    },
    {
        .chain_name = "door_detail_top3",
        .material_name = "door_detail_top",
        .source_texture_count = 2,
        .texture_paths = {
            TITLE_ENVIRONMENT_PATH_PREFIX "door_detail_top3.ia4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "door_detail_top3-mip1.ia4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            2,
        },
    },
    {
        .chain_name = "door_detail_pillar4",
        .material_name = "door_pillar",
        .source_texture_count = 3,
        .texture_paths = {
            TITLE_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4-mip1.i4.sprite",
            TITLE_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4-mip2.i4.sprite",
        },
        .source_start_rdp_levels = {
            0,
            1,
            3,
        },
    },
};

static const rdpq_mipmap_t TITLE_ENVIRONMENT_MIP_MODE = MIPMAP_NEAREST;

static TitleEnvironmentObject s_title_floor;
static TitleEnvironmentObject s_title_walls_back;
static TitleEnvironmentObject s_title_decals_layer1;
static TitleEnvironmentObject s_title_decals_layer2;
static TitleEnvironmentObject s_title_fog_door;

static ScrollParams s_fog_scroll_params = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64,
};

static T3DModel *s_dynamic_banner_model = NULL;
static rspq_block_t *s_dynamic_banner_dpl = NULL;
static T3DMat4FP *s_dynamic_banner_matrix = NULL;
static T3DSkeleton *s_dynamic_banner_skeleton = NULL;
static T3DAnim *s_dynamic_banner_wind_anim = NULL;


static void title_environment_object_clear(TitleEnvironmentObject *object)
{
    if (!object) return;

    object->path = NULL;
    object->model = NULL;
    object->dpl = NULL;
    object->matrix = NULL;
    object->draw_custom = false;
    object->frozen = false;
    object->optimized_mip_draw = false;
    object->scroll_params = NULL;
    object->mip_chains = NULL;
    object->mip_chain_count = 0;
    memset(object->bound_mip_materials, 0, sizeof(object->bound_mip_materials));
}

static void title_environment_clear_handles(void)
{
    title_environment_object_clear(&s_title_floor);
    title_environment_object_clear(&s_title_walls_back);
    title_environment_object_clear(&s_title_decals_layer1);
    title_environment_object_clear(&s_title_decals_layer2);
    title_environment_object_clear(&s_title_fog_door);
}

static void title_environment_object_free(TitleEnvironmentObject *object)
{
    if (!object) return;

    if (object->dpl) {
        rspq_block_free(object->dpl);
        object->dpl = NULL;
    }

    if (object->model) {
        t3d_model_free(object->model);
        object->model = NULL;
    }

    if (object->matrix) {
        free_uncached(object->matrix);
        object->matrix = NULL;
    }

    title_environment_object_clear(object);
}

static bool title_environment_object_load_internal(
    TitleEnvironmentObject *object,
    const char *path,
    bool draw_custom,
    ScrollParams *scroll_params,
    bool frozen
) {
    if (!object || !path) return false;

    title_environment_object_clear(object);

    object->path = path;
    object->draw_custom = draw_custom;
    object->frozen = frozen && !draw_custom;
    object->scroll_params = scroll_params;

    object->model = t3d_model_load(path);
    if (!object->model) {
        debugf("title_scene: failed to load model: %s\n", path);
        return false;
    }

    if (!draw_custom && !object->frozen) {
        rspq_block_begin();
            t3d_model_draw(object->model);
        object->dpl = rspq_block_end();
    }

    object->matrix = malloc_uncached(sizeof(T3DMat4FP));
    if (!object->matrix) {
        debugf("title_scene: failed to allocate matrix: %s\n", path);
        title_environment_object_free(object);
        return false;
    }

    t3d_mat4fp_from_srt_euler(
        object->matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, TITLE_ROOM_Y, 0.0f}
    );

    return true;
}

static bool title_environment_object_load(
    TitleEnvironmentObject *object,
    const char *path,
    bool draw_custom,
    ScrollParams *scroll_params
) {
    return title_environment_object_load_internal(
        object,
        path,
        draw_custom,
        scroll_params,
        false
    );
}

static bool title_environment_object_load_frozen(
    TitleEnvironmentObject *object,
    const char *path
) {
    return title_environment_object_load_internal(
        object,
        path,
        false,
        NULL,
        true
    );
}

static void title_environment_free_mip_chain_sprites(
    TitleEnvironmentMipChain *chain
)
{
    if (!chain) return;

    for (int source_index = 0;
         source_index < TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT;
         ++source_index
    ) {
        if (chain->sprites[source_index]) {
            sprite_free(chain->sprites[source_index]);
            chain->sprites[source_index] = NULL;
        }
    }

    chain->ready = false;
}

static void title_environment_free_mip_chains(
    TitleEnvironmentMipChain *chains,
    int chain_count
)
{
    if (!chains || chain_count <= 0) return;

    for (int chain_index = 0; chain_index < chain_count; ++chain_index) {
        title_environment_free_mip_chain_sprites(&chains[chain_index]);
    }
}

static bool title_environment_mip_chain_uses_palette(
    const TitleEnvironmentMipChain *chain
) {
    if (!chain || !chain->sprites[0]) return false;

    tex_format_t format = sprite_get_format(chain->sprites[0]);
    return format == FMT_CI4 || format == FMT_CI8;
}

static bool title_environment_is_power_of_two_u16(uint16_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static rdpq_texparms_t title_environment_get_texture_params(
    const T3DMaterialTexture *texture
) {
    rdpq_texparms_t params = (rdpq_texparms_t){};

    params.s.translate = texture->s.low;
    params.s.mirror = texture->s.mirror;
    params.s.repeats = REPEAT_INFINITE;
    params.s.scale_log = (int)texture->s.shift;

    if (texture->s.clamp) {
        params.s.repeats = title_environment_is_power_of_two_u16(texture->texWidth)
            ? (texture->s.height - texture->s.low + 1.0f) / (float)texture->texWidth
            : 1.0f;
    }

    params.t.translate = texture->t.low;
    params.t.mirror = texture->t.mirror;
    params.t.repeats = REPEAT_INFINITE;
    params.t.scale_log = (int)texture->t.shift;

    if (texture->t.clamp) {
        params.t.repeats = title_environment_is_power_of_two_u16(texture->texHeight)
            ? (texture->t.height - texture->t.low + 1.0f) / (float)texture->texHeight
            : 1.0f;
    }

    return params;
}

static bool title_environment_load_mip_chain(
    TitleEnvironmentMipChain *chain,
    const T3DMaterial *material
) {
    if (!chain || !material) return false;

    title_environment_free_mip_chain_sprites(chain);

    if (chain->source_texture_count < 2
        || chain->source_texture_count > TITLE_ENVIRONMENT_MAX_MIP_TEXTURE_COUNT
    ) {
        debugf(
            "title_scene: mip chain '%s' has invalid source count %d\n",
            chain->chain_name,
            chain->source_texture_count
        );
        return false;
    }

    if (chain->source_start_rdp_levels[0] != 0) {
        debugf(
            "title_scene: mip chain '%s' source 0 must start at level 0\n",
            chain->chain_name
        );
        return false;
    }

    for (int source_index = 1;
         source_index < chain->source_texture_count;
         ++source_index
    ) {
        uint8_t previous_start =
            chain->source_start_rdp_levels[source_index - 1];
        uint8_t current_start =
            chain->source_start_rdp_levels[source_index];

        if (current_start <= previous_start
            || current_start >= TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT
        ) {
            debugf(
                "title_scene: mip chain '%s' has invalid level ordering\n",
                chain->chain_name
            );
            return false;
        }
    }

    for (int source_index = 0;
         source_index < chain->source_texture_count;
         ++source_index
    ) {
        const char *path = chain->texture_paths[source_index]
            ? chain->texture_paths[source_index]
            : material->textureA.texPath;

        if (!path) {
            debugf(
                "title_scene: mip chain '%s' source %d has no path\n",
                chain->chain_name,
                source_index
            );
            title_environment_free_mip_chain_sprites(chain);
            return false;
        }

        chain->sprites[source_index] = sprite_load(path);
        if (!chain->sprites[source_index]) {
            debugf(
                "title_scene: failed to load mip chain '%s' source %d: %s\n",
                chain->chain_name,
                source_index,
                path
            );
            title_environment_free_mip_chain_sprites(chain);
            return false;
        }
    }

    sprite_t *base = chain->sprites[0];
    tex_format_t base_format = sprite_get_format(base);

    if (base->width != material->textureA.texWidth
        || base->height != material->textureA.texHeight
    ) {
        debugf(
            "title_scene: mip chain '%s' base is %dx%d; model expects %dx%d\n",
            chain->chain_name,
            base->width,
            base->height,
            material->textureA.texWidth,
            material->textureA.texHeight
        );
        title_environment_free_mip_chain_sprites(chain);
        return false;
    }

    if ((base_format == FMT_CI4 || base_format == FMT_CI8)
        && !sprite_get_palette(base)
    ) {
        debugf(
            "title_scene: CI mip chain '%s' source 0 has no palette\n",
            chain->chain_name
        );
        title_environment_free_mip_chain_sprites(chain);
        return false;
    }

    for (int source_index = 1;
         source_index < chain->source_texture_count;
         ++source_index
    ) {
        sprite_t *previous = chain->sprites[source_index - 1];
        sprite_t *current = chain->sprites[source_index];
        uint16_t expected_width = previous->width > 1 ? previous->width / 2 : 1;
        uint16_t expected_height = previous->height > 1 ? previous->height / 2 : 1;

        if (current->width != expected_width || current->height != expected_height) {
            debugf(
                "title_scene: mip chain '%s' source %d is %dx%d; expected %dx%d\n",
                chain->chain_name,
                source_index,
                current->width,
                current->height,
                expected_width,
                expected_height
            );
            title_environment_free_mip_chain_sprites(chain);
            return false;
        }

        if (sprite_get_format(current) != base_format) {
            debugf(
                "title_scene: mip chain '%s' source %d format differs from source 0\n",
                chain->chain_name,
                source_index
            );
            title_environment_free_mip_chain_sprites(chain);
            return false;
        }
    }

    chain->ready = true;
    return true;
}

static bool title_environment_mip_chain_matches_material(
    const TitleEnvironmentMipChain *chain,
    const T3DMaterial *material
) {
    if (!chain || !chain->ready || !chain->sprites[0] || !material) {
        return false;
    }

    const sprite_t *base = chain->sprites[0];

    return base->width == material->textureA.texWidth
        && base->height == material->textureA.texHeight;
}

static void title_environment_upload_mip_chain(
    const TitleEnvironmentMipChain *chain,
    const T3DMaterial *material
) {
    rdpq_texparms_t source_params =
        title_environment_get_texture_params(&material->textureA);
    bool uses_palette = title_environment_mip_chain_uses_palette(chain);
    int source_index = 0;

    rdpq_tex_multi_begin();

    for (int rdp_level = 0;
         rdp_level < TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT;
         ++rdp_level
    ) {
        bool starts_next_source =
            source_index + 1 < chain->source_texture_count
            && rdp_level == chain->source_start_rdp_levels[source_index + 1];

        if (starts_next_source) {
            ++source_index;
            ++source_params.s.scale_log;
            ++source_params.t.scale_log;
            source_params.s.translate *= 0.5f;
            source_params.t.translate *= 0.5f;
        }

        rdpq_tile_t tile = (rdpq_tile_t)(TILE0 + rdp_level);

        if (rdp_level == 0) {
            rdpq_sprite_upload(tile, chain->sprites[source_index], &source_params);
        } else if (starts_next_source) {
            if (uses_palette) {
                surface_t mip_pixels =
                    sprite_get_pixels(chain->sprites[source_index]);
                rdpq_tex_upload(tile, &mip_pixels, &source_params);
            } else {
                rdpq_sprite_upload(
                    tile,
                    chain->sprites[source_index],
                    &source_params
                );
            }
        } else {
            rdpq_tex_reuse(tile, &source_params);
        }
    }

    rdpq_tex_multi_end();
}

static TitleEnvironmentMipChain *title_environment_find_mip_chain(
    T3DMaterial *material,
    TitleEnvironmentMipChain *chains,
    T3DMaterial *const *bound_materials,
    int chain_count
)
{
    if (!material || !chains || !bound_materials || chain_count <= 0) {
        return NULL;
    }

    for (int chain_index = 0; chain_index < chain_count; ++chain_index) {
        if (chains[chain_index].ready
            && bound_materials[chain_index] == material
        ) {
            return &chains[chain_index];
        }
    }

    return NULL;
}

static void title_environment_record_model_with_mipmaps(
    const T3DModel *model,
    TitleEnvironmentMipChain *chains,
    T3DMaterial *const *bound_materials,
    int chain_count
) {
    T3DModelState state = t3d_model_state_create();
    T3DModelIter iterator = t3d_model_iter_create(
        model,
        T3D_CHUNK_TYPE_OBJECT
    );
    TitleEnvironmentMipChain *active_chain = NULL;
    T3DMaterial *active_material = NULL;

    while (t3d_model_iter_next(&iterator)) {
        T3DObject *model_object = iterator.object;
        T3DMaterial *material = model_object->material;

        if (material) {
            TitleEnvironmentMipChain *requested_chain =
                title_environment_find_mip_chain(
                    material,
                    chains,
                    bound_materials,
                    chain_count
                );

            if (requested_chain && requested_chain != active_chain) {
                title_environment_upload_mip_chain(requested_chain, material);
                rdpq_mode_mipmap(
                    TITLE_ENVIRONMENT_MIP_MODE,
                    TITLE_ENVIRONMENT_MIP_RDP_LEVEL_COUNT
                );

                state.lastTextureHashA = material->textureA.textureHash;
                state.lastTextureHashB = material->textureB.textureHash;
                state.lastRenderFlags = ~material->renderFlags;

                active_chain = requested_chain;
                active_material = material;
            } else if (!requested_chain && active_chain) {
                rdpq_mode_mipmap(MIPMAP_NONE, 0);

                if (title_environment_mip_chain_uses_palette(active_chain)) {
                    rdpq_mode_tlut(TLUT_NONE);
                }

                state.lastRenderFlags = ~material->renderFlags;
                active_chain = NULL;
                active_material = NULL;
            }

            t3d_model_draw_material(material, &state);
        }

        t3d_model_draw_object(model_object, NULL);
    }

    if (active_chain) {
        rdpq_mode_mipmap(MIPMAP_NONE, 0);

        if (title_environment_mip_chain_uses_palette(active_chain)) {
            rdpq_mode_tlut(TLUT_NONE);
        }

        t3d_state_set_drawflags(active_material->renderFlags);
    }

    if (state.lastVertFXFunc != T3D_VERTEX_FX_NONE) {
        t3d_state_set_vertex_fx(T3D_VERTEX_FX_NONE, 0, 0);
    }
}

static void title_environment_record_object_commands(
    const TitleEnvironmentObject *object
) {
    if (!object || !object->model) return;

    if (object->optimized_mip_draw) {
        title_environment_record_model_with_mipmaps(
            object->model,
            object->mip_chains,
            object->bound_mip_materials,
            object->mip_chain_count
        );
        return;
    }

    t3d_model_draw(object->model);
}

static bool title_environment_record_frozen_block(
    TitleEnvironmentObject *object
) {
    if (!object || !object->model) return false;

    if (object->dpl) {
        rdpq_call_deferred(
            (void (*)(void *))rspq_block_free,
            object->dpl
        );
        object->dpl = NULL;
    }

    rspq_block_begin_frozen(NULL);
        title_environment_record_object_commands(object);
    object->dpl = rspq_block_end_frozen();

    return object->dpl != NULL;
}

static bool title_environment_load_mip_model(
    TitleEnvironmentObject *object,
    const char *model_path,
    const char *model_name,
    TitleEnvironmentMipChain *chains,
    int chain_count,
    bool reset_mip_sprites
)
{
    if (!object || !model_path || !model_name || !chains || chain_count <= 0) {
        return false;
    }

    if (reset_mip_sprites) {
        title_environment_free_mip_chains(chains, chain_count);
    }

    if (!title_environment_object_load_internal(
            object,
            model_path,
            true,
            NULL,
            true
        )
    ) {
        return false;
    }

    int ready_chain_count = 0;
    T3DMaterial *bound_materials[TITLE_ENVIRONMENT_MAX_MIP_CHAIN_COUNT] = {NULL};

    for (int chain_index = 0; chain_index < chain_count; ++chain_index) {
        TitleEnvironmentMipChain *chain = &chains[chain_index];
        T3DMaterial *material = t3d_model_get_material(
            object->model,
            chain->material_name
        );

        if (!material || !material->textureA.texPath) {
            continue;
        }

        if (material->textureB.texPath || material->textureB.texReference) {
            continue;
        }

        bool available = chain->ready
            ? title_environment_mip_chain_matches_material(chain, material)
            : title_environment_load_mip_chain(chain, material);

        if (!available) {
            continue;
        }

        bound_materials[chain_index] = material;
        ++ready_chain_count;
    }

    object->draw_custom = false;
    object->frozen = true;
    object->optimized_mip_draw = ready_chain_count > 0;
    object->mip_chains = object->optimized_mip_draw ? chains : NULL;
    object->mip_chain_count = object->optimized_mip_draw
        ? (uint8_t)chain_count
        : 0;
    memcpy(
        object->bound_mip_materials,
        bound_materials,
        sizeof(object->bound_mip_materials)
    );

    return true;
}

static void title_environment_object_draw(TitleEnvironmentObject *object)
{
    if (!object || !object->matrix || !object->model) return;

    t3d_matrix_set(object->matrix, true);

    if (object->draw_custom) {
        t3d_model_draw_custom(object->model, (T3DModelDrawConf){
            .userData = object->scroll_params,
            .tileCb = object->scroll_params ? tile_scroll : NULL,
        });
        return;
    }

    if (object->frozen) {
        if (rspq_block_run_frozen(object->dpl)) {
            return;
        }

        if (!title_environment_record_frozen_block(object)) {
            debugf(
                "title_scene: failed to record frozen block for %s\n",
                object->path ? object->path : "<unknown>"
            );
            return;
        }

        if (!rspq_block_run_frozen(object->dpl)) {
            title_environment_record_object_commands(object);
        }

        return;
    }

    if (object->dpl) {
        rspq_block_run(object->dpl);
    }
}

static void title_environment_load(void)
{
    title_environment_load_mip_model(
        &s_title_floor,
        TITLE_ENVIRONMENT_PATH_PREFIX "test-floor-opt.t3dm",
        "test-floor-opt",
        s_title_floor_mip_chains,
        TITLE_ENVIRONMENT_FLOOR_MIP_CHAIN_COUNT,
        true
    );

    title_environment_load_mip_model(
        &s_title_walls_back,
        TITLE_ENVIRONMENT_PATH_PREFIX "test-walls_back-opt.t3dm",
        "test-walls_back-opt",
        s_title_walls_back_mip_chains,
        TITLE_ENVIRONMENT_WALLS_BACK_MIP_CHAIN_COUNT,
        true
    );

    title_environment_object_load_frozen(
        &s_title_decals_layer1,
        TITLE_ENVIRONMENT_PATH_PREFIX "test-decals_layer1-opt.t3dm"
    );
    title_environment_object_load_frozen(
        &s_title_decals_layer2,
        TITLE_ENVIRONMENT_PATH_PREFIX "test-decals_layer2-opt.t3dm"
    );

    title_environment_object_load(
        &s_title_fog_door,
        TITLE_ENVIRONMENT_PATH_PREFIX "test-fog_door.t3dm",
        true,
        &s_fog_scroll_params
    );
}

static void title_environment_free(void)
{
    title_environment_object_free(&s_title_floor);
    title_environment_free_mip_chains(
        s_title_floor_mip_chains,
        TITLE_ENVIRONMENT_FLOOR_MIP_CHAIN_COUNT
    );

    title_environment_object_free(&s_title_walls_back);
    title_environment_free_mip_chains(
        s_title_walls_back_mip_chains,
        TITLE_ENVIRONMENT_WALLS_BACK_MIP_CHAIN_COUNT
    );

    title_environment_object_free(&s_title_decals_layer1);
    title_environment_object_free(&s_title_decals_layer2);
    title_environment_object_free(&s_title_fog_door);
}

static void title_scene_make_matrix(T3DMat4FP **matrix)
{
    if (!*matrix) {
        *matrix = malloc_uncached(sizeof(T3DMat4FP));
    }

    t3d_mat4fp_from_srt_euler(
        *matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, TITLE_ROOM_Y, 0.0f}
    );
}

static void title_scene_load_room_assets(void)
{
    title_environment_load();
}


static void title_scene_load_dynamic_banner_assets(void)
{
    if (s_dynamic_banner_model) {
        return;
    }

    s_dynamic_banner_model = t3d_model_load("rom:/title_screen/dynamic_banners.t3dm");

    s_dynamic_banner_skeleton = malloc_uncached(sizeof(T3DSkeleton));
    *s_dynamic_banner_skeleton = t3d_skeleton_create(s_dynamic_banner_model);

    s_dynamic_banner_wind_anim = malloc_uncached(sizeof(T3DAnim));
    *s_dynamic_banner_wind_anim = t3d_anim_create(s_dynamic_banner_model, "Wind");

    t3d_anim_set_looping(s_dynamic_banner_wind_anim, true);
    t3d_anim_set_playing(s_dynamic_banner_wind_anim, true);
    t3d_anim_attach(s_dynamic_banner_wind_anim, s_dynamic_banner_skeleton);

    rspq_block_begin();
        t3d_model_draw_skinned(s_dynamic_banner_model, s_dynamic_banner_skeleton);
    s_dynamic_banner_dpl = rspq_block_end();

    title_scene_make_matrix(&s_dynamic_banner_matrix);
}

static void title_scene_free_dynamic_banner_assets(void)
{
    if (s_dynamic_banner_dpl) {
        rspq_block_free(s_dynamic_banner_dpl);
        s_dynamic_banner_dpl = NULL;
    }

    if (s_dynamic_banner_wind_anim) {
        t3d_anim_destroy(s_dynamic_banner_wind_anim);
        free_uncached(s_dynamic_banner_wind_anim);
        s_dynamic_banner_wind_anim = NULL;
    }

    if (s_dynamic_banner_skeleton) {
        t3d_skeleton_destroy(s_dynamic_banner_skeleton);
        free_uncached(s_dynamic_banner_skeleton);
        s_dynamic_banner_skeleton = NULL;
    }

    if (s_dynamic_banner_model) {
        t3d_model_free(s_dynamic_banner_model);
        s_dynamic_banner_model = NULL;
    }

    if (s_dynamic_banner_matrix) {
        free_uncached(s_dynamic_banner_matrix);
        s_dynamic_banner_matrix = NULL;
    }
}

static void title_scene_reset_runtime_state(void)
{
    s_result = TITLE_SCENE_RESULT_NONE;
    s_state = TITLE_STATE_IDLE;

    s_screen_transition = false;
    s_screen_breath = false;
    s_skip_button_visible = false;
    s_last_cutscene_a_pressed = btn.a;
    s_last_a_pressed = btn.a;
    s_last_start_pressed = btn.start;

    s_current_title_dialog = 0;
    s_title_text_activation_timer = 0.0f;
    s_title_start_game_timer = 0.0f;
}

static void title_scene_setup_camera_and_character(void)
{
    cameraState = CAMERA_CUSTOM;
    lastCameraState = CAMERA_CUSTOM;

    camera_mode(CAMERA_CUSTOM);
    camera_initialize(
        &(T3DVec3){{-580.6f, 75.0f, 0.0f}},
        &(T3DVec3){{-1.0f, 0.0f, 0.0f}},
        1.544792654048f,
        4.05f
    );

    customCamTarget.v[1] = 90.0f;

    character.pos[0] = -650.0f;
    character.pos[1] = 44.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 1.5f;
    character.scale[1] = MODEL_SCALE * 1.5f;
    character.scale[2] = MODEL_SCALE * 1.5f;

    character.rot[1] = TITLE_CHARACTER_YAW;

    character_update_position();
    character_set_state(CHAR_STATE_TITLE_IDLE);
    character_set_velocity_xz(0.0f, 0.0f);
}

static void title_scene_start_dialog(void)
{
    s_current_title_dialog = 0;
    s_title_text_activation_timer = 0.0f;

    dialog_controller_speak(TITLE_DIALOGS[0], 0, 9.0f, false, true);
}

void title_scene_enter(void)
{
    joypad_rumble_stop();

    audio_scene_load_paths(TITLE_SFX_PATHS, TITLE_SFX_COUNT);

    game_lighting_initialize();
    colorAmbient[0] = 0xFF;
    colorAmbient[1] = 0xFF;
    colorAmbient[2] = 0xFF;
    colorAmbient[3] = 0xFF;

    dialog_controller_init();
    button_prompt_init();
    letterbox_init();
    letterbox_show(false);

    title_environment_clear_handles();
    title_scene_load_room_assets();
    title_scene_load_dynamic_banner_assets();

    title_scene_reset_runtime_state();
    title_scene_setup_camera_and_character();

    if (s_dynamic_banner_wind_anim) {
        t3d_anim_set_time(s_dynamic_banner_wind_anim, 0.0f);
        t3d_anim_set_playing(s_dynamic_banner_wind_anim, true);
    }

    audio_play_music("rom:/audio/music/demonous-22k.wav64", true);

    title_scene_start_dialog();

    startScreenFade = true;
}

void title_scene_exit(void)
{
    audio_stop_all_sfx();
    audio_stop_music();

    dialog_controller_reset();
    menu_controller_close();

    camera_breath_active(false);

    rspq_wait();

    title_scene_free_dynamic_banner_assets();

    title_environment_free();

    s_state = TITLE_STATE_INACTIVE;
    s_result = TITLE_SCENE_RESULT_NONE;

    s_screen_transition = false;
    s_screen_breath = false;
    s_skip_button_visible = false;
}

void title_scene_begin_transition(void)
{
    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN) {
        return;
    }

    if (s_state != TITLE_STATE_IDLE) {
        return;
    }

    menu_controller_close();

    s_state = TITLE_STATE_TRANSITION_TO_GUARDIAN;

    character_set_state(CHAR_STATE_FOG_WALK);

    s_skip_button_visible = false;
    s_last_cutscene_a_pressed = btn.a;

    camera_breath_active(false);
    s_screen_breath = false;

    audio_stop_music_fade(6);

    audio_scene_load_paths(TITLE_SFX_PATHS, TITLE_SFX_COUNT);

    audio_play_scene_sfx_dist(
        TITLE_SFX_WALK,
        1.0f,
        0.0f
    );
}

static void title_scene_finish_transition(void)
{
    s_title_start_game_timer = 0.0f;
    s_skip_button_visible = false;
    s_state = TITLE_STATE_DONE;
    s_result = TITLE_SCENE_RESULT_START_GUARDIAN_INTRO;

    character_set_velocity_xz(0.0f, 0.0f);

    audio_stop_all_sfx();
}

static void title_scene_update_transition(void)
{
    if (s_title_start_game_timer >= TITLE_START_GAME_TIME) {
        title_scene_finish_transition();
        return;
    }

    bool a_currently_pressed = btn.a;
    bool a_just_pressed = a_currently_pressed && !s_last_cutscene_a_pressed;

    if (a_just_pressed) {
        if (!s_skip_button_visible) {
            s_skip_button_visible = true;
        } else {
            s_last_cutscene_a_pressed = a_currently_pressed;
            title_scene_finish_transition();
            return;
        }
    }

    s_last_cutscene_a_pressed = a_currently_pressed;

    if (btn.start) {
        title_scene_finish_transition();
        return;
    }

    audio_update_fade(deltaTime);

    character_set_state(CHAR_STATE_FOG_WALK);

    s_title_start_game_timer += deltaTime;

    const float targetDropSpeed = 1.0f;

    customCamDir.v[0] = customCamTarget.v[0] - customCamPos.v[0];
    customCamDir.v[1] = customCamTarget.v[1] - customCamPos.v[1];
    customCamDir.v[2] = customCamTarget.v[2] - customCamPos.v[2];
    t3d_vec3_norm(&customCamDir);

    for (int i = 0; i < 3; i++) {
        customCamPos.v[i]    += customCamDir.v[i] * TITLE_CAMERA_FORWARD_SPEED * deltaTime;
        customCamTarget.v[i] += customCamDir.v[i] * TITLE_CAMERA_FORWARD_SPEED * deltaTime;
    }

    customCamTarget.v[1] -= targetDropSpeed * deltaTime;
}

static void title_scene_update_idle(void)
{
    s_last_start_pressed = btn.start;
    s_last_a_pressed = btn.a;

    if (!s_screen_breath) {
        camera_breath_active(true);
        s_screen_breath = true;
    }

    camera_breath_update(deltaTime);

    if (!menu_controller_is_title_submenu_active()) {
        if (s_title_text_activation_timer >= TITLE_TEXT_ACTIVATION_TIME) {
            dialog_controller_update();

            if (!dialog_controller_speaking()) {
                s_current_title_dialog++;

                if (s_current_title_dialog >= TITLE_DIALOG_COUNT) {
                    s_title_text_activation_timer = 0.0f;
                    s_current_title_dialog = -1;
                } else {
                    dialog_controller_speak(
                        TITLE_DIALOGS[s_current_title_dialog],
                        0,
                        9.0f,
                        false,
                        true
                    );
                }
            }
        } else {
            s_title_text_activation_timer += deltaTime;
        }
    }
}

void title_scene_update(void)
{
    if (s_state == TITLE_STATE_INACTIVE || s_state == TITLE_STATE_DONE) {
        return;
    }

    audio_update_fade(deltaTime);
    scroll_update();

    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN) {
        title_scene_update_transition();
    } else {
        menu_controller_update_title();

        MenuAction action = menu_controller_consume_action();
        if (action == MENU_ACTION_TITLE_START_GAME) {
            title_scene_begin_transition();
        } else {
            title_scene_update_idle();
        }
    }

    character_update_cinematic();

    if (s_dynamic_banner_wind_anim && s_dynamic_banner_skeleton) {
        t3d_anim_update(s_dynamic_banner_wind_anim, deltaTime);
        t3d_skeleton_update(s_dynamic_banner_skeleton);
    }

    letterbox_update();
}

static void title_scene_draw_3d(void)
{
    t3d_matrix_push_pos(1);

        // The title shot only uses the optimized back-room model.
        rdpq_mode_zbuf(false, false);
        title_environment_object_draw(&s_title_walls_back);
        title_environment_object_draw(&s_title_floor);

        // Both decal layers draw in their authored order with depth disabled.
        rdpq_mode_zbuf(false, false);
        title_environment_object_draw(&s_title_decals_layer1);
        title_environment_object_draw(&s_title_decals_layer2);

        if (s_dynamic_banner_matrix && s_dynamic_banner_dpl) {
            t3d_matrix_set(s_dynamic_banner_matrix, true);
            rspq_block_run(s_dynamic_banner_dpl);
        }

        rdpq_mode_zbuf(true, true);
        character_draw();

        rdpq_mode_zbuf(true, false);
        title_environment_object_draw(&s_title_fog_door);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}


static void title_scene_draw_run_counter_panel(void)
{
    const bool savesOn = save_controller_is_enabled();
    const uint32_t savedRuns = save_controller_get_run_count();
    const uint32_t bestMs = save_controller_get_best_boss_time_ms();

    if (menu_controller_is_title_submenu_active()) {
        return;
    }

    if (savesOn && savedRuns == 0 && bestMs == 0) {
        return;
    }

    const int margin = ui_safe_margin_x();
    const int panelW = 120;
    const bool showBest = savesOn && bestMs > 0;
    const int lineCount = 1 + (showBest ? 1 : 0);
    const int panelH = 6 + (lineCount * 13);
    const int panelX0 = margin;
    const int panelY0 = SCREEN_HEIGHT - ui_safe_margin_y() - panelH;

    rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_fill_rectangle(panelX0, panelY0, panelX0 + panelW, panelY0 + panelH);

    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

    int lineY = panelY0 + 13;

    if (savesOn) {
        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Runs: %lu",
            (unsigned long)savedRuns
        );
    } else {
        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Runs: --"
        );
    }

    lineY += 13;

    if (showBest) {
        const uint32_t minutes = bestMs / 60000u;
        const uint32_t seconds = (bestMs / 1000u) % 60u;
        const uint32_t millis  = bestMs % 1000u;

        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Best: %lu:%02lu.%03lu",
            (unsigned long)minutes,
            (unsigned long)seconds,
            (unsigned long)millis
        );
    }
}

static void title_scene_draw_idle_2d(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    title_scene_draw_run_counter_panel();

    if (
        s_title_text_activation_timer >= TITLE_TEXT_ACTIVATION_TIME &&
        !menu_controller_is_title_submenu_active()
    ) {
        const int dlgW = 120;
        const int dlgH = 180;
        const int dlgX = SCREEN_WIDTH - ui_safe_margin_x() - dlgW;
        const int dlgY = ui_safe_margin_y();

        dialog_controller_draw(true, dlgX, dlgY, dlgW, dlgH);
    }

    display_utility_solid_black_transition(true, 200.0f);
}

static void title_scene_draw_transition_2d(void)
{
    if (s_title_start_game_timer >= TITLE_FADE_TIME && !s_screen_transition) {
        startScreenFade = true;
        s_screen_transition = true;
    }

    if (s_screen_transition) {
        display_utility_solid_black_transition(false, 200.0f);
    }

    cutscene_manager_draw_skip_overlay(s_skip_button_visible);
}

void title_scene_draw(T3DViewport *viewport)
{
    if (s_state == TITLE_STATE_INACTIVE) {
        return;
    }

    t3d_frame_start();

    if (!DITHER_ENABLED && !debugDraw) {
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    }

    t3d_viewport_attach(viewport);

    color_t fogColor = (color_t){0, 0, 0, 0xFF};
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    t3d_fog_set_range(450.0f, 800.0f);
    t3d_fog_set_enabled(true);

    t3d_light_set_ambient(colorAmbient);

    title_scene_draw_3d();

    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN || s_state == TITLE_STATE_DONE) {
        title_scene_draw_transition_2d();
    } else {
        title_scene_draw_idle_2d();
    }
}

TitleSceneResult title_scene_get_result(void)
{
    return s_result;
}

void title_scene_clear_result(void)
{
    s_result = TITLE_SCENE_RESULT_NONE;
}

bool title_scene_is_active(void)
{
    return s_state != TITLE_STATE_INACTIVE;
}

bool title_scene_is_transitioning(void)
{
    return s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN;
}