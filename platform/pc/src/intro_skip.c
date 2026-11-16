/* Skip intro (docs/platforms.md, "Skip intro"; General's Skip intro): the
 * game boots straight to the title screen.
 *
 * MainLoop (main_loop.cpp) plays the legal notice (PlayBootCutscene), the
 * company logos (ShowCompanyLogos), the language select
 * (Open/Run/CloseLanguageSelect) and the intro cutscene
 * (PlayIntroCutscene), then the title. The link wraps them (platform.mk,
 * --wrap; MainLoop is their only caller): with the setting on, each does
 * nothing, and the language is the one last picked (settings.ini's
 * language, written each time the language select runs; English before
 * that). The setting is read as the game starts, so a change counts from
 * the next launch. PlayBootCutscene's wrap is cutscene_pick.c's, which
 * asks HostSkipIntro too. */

#include "gba/gba.h"
#include "host_platform.h"
#include "host_ui.h"

extern s32 gLanguage;

void __real__ZN4Game5Level10LevelState16ShowCompanyLogosEv(void *self);
void __real__ZN4Game5Level10LevelState17PlayIntroCutsceneEv(void *self);
void __real_OpenLanguageSelect(void);
s32 __real_RunLanguageSelect(void);
void __real_CloseLanguageSelect(void);

/* Read once, at the first boot step: a change in the settings menu waits
 * for the next launch. --cutscene (a cutscene to watch) plays the boot. */
int HostSkipIntro(void)
{
    static int skip = -1;

    if (skip < 0) {
        skip = gHostSettings.skipIntro && gHostOptions.cutscene < 0;
    }
    return skip;
}

void __wrap__ZN4Game5Level10LevelState16ShowCompanyLogosEv(void *self)
{
    if (!HostSkipIntro()) {
        __real__ZN4Game5Level10LevelState16ShowCompanyLogosEv(self);
    }
}

void __wrap_OpenLanguageSelect(void)
{
    if (!HostSkipIntro()) {
        __real_OpenLanguageSelect();
    }
}

s32 __wrap_RunLanguageSelect(void)
{
    s32 language;

    if (HostSkipIntro()) {
        return gHostSettings.language;
    }
    language = __real_RunLanguageSelect();
    /* remembered for the boots that skip it */
    if (language != gHostSettings.language && !gHostOptions.headless) {
        gHostSettings.language = language;
        HostConfigSave();
    }
    return language;
}

void __wrap_CloseLanguageSelect(void)
{
    if (!HostSkipIntro()) {
        __real_CloseLanguageSelect();
    }
}

void __wrap__ZN4Game5Level10LevelState17PlayIntroCutsceneEv(void *self)
{
    if (!HostSkipIntro()) {
        __real__ZN4Game5Level10LevelState17PlayIntroCutsceneEv(self);
    }
}
