#include <libdragon.h>
#include <ctype.h>
#include "dialog_controller.h"
#include "game_time.h"
#include "globals.h"
#include "general_utility.h"

#define MAX_TEXT_LENGTH 256
#define SLOW_SPEED 0.2f
#define NORMAL_SPEED 0.12f
#define FAST_SPEED 0.04f
#define BREATH_SPEED 1.0f

static int maxCharacters = 0;

static int currentWordIndex = 0;
static float currentSpeed = 0.08f;

static int checkedCharacters = 0;
static int visibleCharacters = 0;
static char visibleText[MAX_TEXT_LENGTH]; // Buffer to hold the visible text

static float dialogTimer = 0.0f;
static float dialogActiveTimer = 0.0f;
static float dialogActiveTime = 5.0f;

static sprite_t *dialogBox;
static surface_t dialogBoxSurf;

static const char *dialogText = "meep.";

static bool showDialog = false;
static bool endDialog = true;

void dialog_controller_reset(void)
{
    maxCharacters = 0;
    currentWordIndex = 0;
    currentSpeed = 0.08f;
    checkedCharacters = 0;
    visibleCharacters = 0;
    dialogTimer = 0.0f;
    dialogActiveTimer = 0.0f;
    dialogActiveTime = 5.0f;
    dialogText = "meep.";
    showDialog = false;
    endDialog = true;
    
    // Clear the visible text buffer
    visibleText[0] = '\0';
}

// TODO: TEMP
sprite_t *dialog4Text;
surface_t dialog4TextSurf;

static int count_printable_glyphs(const char* str) {
    int src_index = 0;
    int count = 0;
    while (str[src_index] != '\0') {
        char c = str[src_index];
        if (c == '<' || c == '^' || c == '>' || c == '~' || c == '@' || c == ' ' || c == '\n') {
            src_index++;
            continue;
        }
        // Simple single-byte character counting for now
        src_index += 1;
        count++;
    }
    return count;
}

// Fills visibleDialog with the first N visible glyphs (not counting control codes)
static void build_clean_dialog(char* out, const char* src) {
    int src_index = 0, dst_index = 0;
    while (src[src_index]) {
        char c = src[src_index];
        if (c == '<' || c == '^' || c == '>' || c == '~' || c == '@') {
            src_index++;
            continue;
        }
        // Simple single-byte character copying for now
        out[dst_index++] = src[src_index++];
    }
    out[dst_index] = '\0';
}

void dialog_controller_speak(const char* text, int style, float activeTime, bool interactable, bool end) 
{
    dialogActiveTime = activeTime;
    dialogText = text;
    maxCharacters = count_printable_glyphs(dialogText);
    currentSpeed = FAST_SPEED;
    visibleCharacters = 0;
    checkedCharacters = 0;
    dialogTimer = 0.0f;
    showDialog = true;
    endDialog = end;
}

bool dialog_controller_speaking(void) 
{
    return showDialog;
}

void dialog_controller_stop_speaking(void) 
{
    showDialog = false;
    dialogActiveTimer = 0.0f; // Reset the timer
}

// Never call this again.
void dialog_controller_init(void) 
{
    dialogBox = sprite_load("rom:/dialog.ia8.sprite");
    dialogBoxSurf = sprite_get_pixels(dialogBox);
    // Font is already registered in main.c, so we don't need to load it again

    // TODO: TEMP
    // dialog4Text = sprite_load("rom:/temp_king_line.sprite");
    // dialog4TextSurf = sprite_get_pixels(dialog4Text);
}

void dialog_controller_draw_dialog(bool bottom, int width, int height) 
{

    int x = (SCREEN_WIDTH - width) / 2;
    int y = (SCREEN_HEIGHT - height) / 2;
    int paddingX = 20;
    int paddingY = 10;

    if (bottom) 
    {
        // bottom positioning
        y = 240 - height - 10;
    }
    else
    {
        // top positioning
        y = 10;
    }

    rdpq_sync_pipe(); // possibly needed for hardware
    rdpq_set_mode_standard();
    // rdpq_set_mode_copy(true);
    //rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(1);
    //rdpq_mode_dithering(DITHER_SQUARE_SQUARE);

    // Blender mode for proper alpha blending
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    //rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,0),(0,0,0, TEX0)));

    // Blit the sprite with scale applied
    rdpq_tex_blit(&dialogBoxSurf, x, y, &(rdpq_blitparms_t)
    {
        .scale_x = 1.0f,
        .scale_y = 1.0f,
    });

    int src_index = 0;
    int dst_index = 0;
    int chars_copied = 0;
    
    while (chars_copied < visibleCharacters && dialogText[src_index] != '\0') 
    {
        // Get the current character
        char c = dialogText[src_index];
    
        // Check for special speed control characters and update the speed
        if (c == '<') {
            currentSpeed = SLOW_SPEED;  // Set slow speed
            src_index++;  // Skip the < character
            continue;  // Skip this iteration and check the next character
        }
        else if (c == '^') {
            currentSpeed = NORMAL_SPEED;  // Set normal speed
            src_index++;  // Skip the ^ character
            continue;  // Skip this iteration and check the next character
        }
        else if (c == '>') {
            currentSpeed = FAST_SPEED;  // Set fast speed
            src_index++;  // Skip the > character
            continue;  // Skip this iteration and check the next character
        } 
        else if (c == '@') { // TODO: TEMP
            if (dialog4Text) {
                rdpq_set_mode_copy(true);
                rdpq_mode_alphacompare(128);
                //rdpq_sync_load(); // possibly needed for hardware
                rdpq_tex_blit(&dialog4TextSurf, x, y, &(rdpq_blitparms_t)
                {
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                });
            }

            src_index++;  // Skip the @ character
            continue;  // Skip this iteration and check the next character
        }
        
        
        // Skip the '~' character (which indicates a pause or special effect)
        if (c == '~') {
            currentSpeed = BREATH_SPEED; 
            src_index += 1; // Skip the ~ (1 byte)
            continue;
        }

        int char_len = 1; // Simple single-byte character length for now
    
        // Copy the full UTF-8 character (could be multiple bytes)
        for (int i = 0; i < char_len; i++) {
            visibleText[dst_index++] = dialogText[src_index++];
        }
        
        chars_copied++;
    }
    
    visibleText[dst_index] = '\0';

    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_LEFT, 
        .width = width - (paddingX * 2), 
        .height = height,
        .wrap = WRAP_WORD,
      }, FONT_UNBALANCED, x + paddingX, y + paddingY, visibleText);
}

void dialog_controller_update(void) 
{
    if(!showDialog) return;

    if(dialogActiveTimer >= dialogActiveTime && endDialog) 
    {
        // Check if the dialog is finished
        if (visibleCharacters >= maxCharacters) 
        {
            showDialog = false;
            dialogActiveTimer = 0.0f; // Reset the timer
            return;
        }
    }
    
    if(dialogActiveTime != 0.0f || endDialog == true) 
    {
        dialogActiveTimer += deltaTime;
    }

    maxCharacters = strlen(dialogText);

    if (visibleCharacters < maxCharacters) 
    {
        dialogTimer += deltaTime;
        char c = dialogText[visibleCharacters];

        if (c == ' ' || c == '\n') 
        {
            currentWordIndex++;
            visibleCharacters++;
            dialogTimer = 0.0f;
            return;
        }

        if (dialogTimer >= currentSpeed) 
        {
            dialogTimer = 0.0f; // Reset the timer
            visibleCharacters++;
        }
    }
}

void dialog_controller_draw(void) 
{
    if(showDialog)
    {
        int box_width = 220;
        int box_height = 70;

        dialog_controller_draw_dialog(true, box_width, box_height);
    }
}

// No need to call this function, it's here for completeness
void dialog_controller_free(void) 
{
    rspq_wait(); // Ensure all rendering is done before freeing resources
    if (dialogBox) {
        sprite_free(dialogBox);
        dialogBox = NULL;
        surface_free(&dialogBoxSurf);
    }
    if (dialog4Text) {
        sprite_free(dialog4Text);
        dialog4Text = NULL;
        surface_free(&dialog4TextSurf);
    }
    // Font is managed by main.c, so we don't free it here
}