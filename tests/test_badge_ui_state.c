#include "badge_ui_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static badge_data_t data;
static badge_config_t config;

int main(void)
{
    badge_config_defaults(&config);
    snprintf(config.username,sizeof(config.username),"octocat");
    data.repo_count=3;data.scope_count=7;
    badge_ui_state_t s={0};
    badge_action_t action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(action.kind==BADGE_ACTION_NONE&&s.modal==BADGE_MODAL_PROFILE);
    badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);
    assert(s.page==0&&s.modal==BADGE_MODAL_PROFILE);
    badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
    assert(s.modal==BADGE_MODAL_NONE&&s.settings==BADGE_SETTINGS_CLOSED);

    badge_ui_state_key(&s,BADGE_KEY_UP,true,&data,&config);
    assert(s.page==6);
    badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);
    assert(s.page==0);
    badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);
    assert(s.page==1&&s.focus==1);
    data.repo_count=1;badge_ui_state_clamp(&s,&data);assert(s.focus==0);
    data.repo_count=0;badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);assert(s.focus==0);

    badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);
    badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);
    assert(s.page==2&&s.language==6);
    data.scope_count=2;badge_ui_state_clamp(&s,&data);assert(s.language==0);
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);assert(s.language==1);

    s.page=4;s.focus=0;
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);
    assert(s.focus==1&&s.pr_tab==0); /* focus does not activate a tab */
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);assert(s.pr_tab==1);
    s.page=5;s.focus=0;
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);assert(s.star_tab==1);

    badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
    assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==0);
    uint8_t page=s.page;
    badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);assert(s.page==page&&s.focus==0);
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.settings==BADGE_SETTINGS_BRIGHTNESS&&s.focus==0);
    const uint8_t brightness[]={100,80,60,40,20,10};
    for(unsigned i=0;i<sizeof(brightness)/sizeof(brightness[0]);++i) {
        s.focus=(uint8_t)i;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_BRIGHTNESS&&action.value==brightness[i]);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==0);
        config.brightness=action.value;
        badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(s.settings==BADGE_SETTINGS_BRIGHTNESS&&s.focus==i);
        /* Confirming the already-selected value also returns to its main row. */
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_BRIGHTNESS&&action.value==brightness[i]);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==0);
        badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    }
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);assert(s.focus==0);
    badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);assert(s.focus==5);
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==0);
    s.focus=1;badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.settings==BADGE_SETTINGS_SCREEN_TIMEOUT&&s.focus==2);
    const uint8_t timeouts[]={1,5,10,30,60,0};
    for(unsigned i=0;i<sizeof(timeouts)/sizeof(timeouts[0]);++i) {
        /* Every offered value is selectable, including NEVER at index five. */
        s.focus=(uint8_t)i;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_SCREEN_TIMEOUT&&action.value==timeouts[i]);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==1);
        config.screen_timeout_min=action.value;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_NONE&&s.settings==BADGE_SETTINGS_SCREEN_TIMEOUT&&s.focus==i);
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_SCREEN_TIMEOUT&&action.value==timeouts[i]);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==1);
        badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    }
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);assert(s.focus==0);
    badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);assert(s.focus==5);
    action=badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
    assert(action.kind==BADGE_ACTION_NONE&&s.settings==BADGE_SETTINGS_CLOSED);
    badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
    s.focus=2;badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.settings==BADGE_SETTINGS_THEME);
    for(unsigned i=0;i<5;++i) {
        s.focus=(uint8_t)i;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_THEME&&action.value==i);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==2);
        config.theme=action.value;
        badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(s.settings==BADGE_SETTINGS_THEME&&s.focus==i);
        action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
        assert(action.kind==BADGE_ACTION_THEME&&action.value==i);
        assert(s.settings==BADGE_SETTINGS_MAIN&&s.focus==2);
        badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    }
    badge_ui_state_key(&s,BADGE_KEY_DOWN,false,&data,&config);assert(s.focus==0);
    badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);assert(s.focus==4);
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    s.focus=3;action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(action.kind==BADGE_ACTION_PROVISION&&s.modal==BADGE_MODAL_WIFI);
    badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);assert(s.modal==BADGE_MODAL_WIFI&&s.focus==3);
    action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(action.kind==BADGE_ACTION_CLOSE_PROVISION&&s.modal==BADGE_MODAL_NONE);
    s.focus=4;action=badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(action.kind==BADGE_ACTION_SYNC&&s.settings==BADGE_SETTINGS_MAIN);
    /* Leaving settings or navigating after REFETCH does not emit an action that
     * could stop its worker, and long OK never activates the focused command. */
    action=badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
    assert(action.kind==BADGE_ACTION_NONE&&s.settings==BADGE_SETTINGS_CLOSED);
    action=badge_ui_state_key(&s,BADGE_KEY_DOWN,true,&data,&config);
    assert(action.kind==BADGE_ACTION_NONE);
    for(unsigned focus=0;focus<5;++focus) {
        badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
        s.focus=(uint8_t)focus;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
        assert(action.kind==BADGE_ACTION_NONE&&s.settings==BADGE_SETTINGS_CLOSED);
    }
    const badge_settings_t submenus[]={BADGE_SETTINGS_BRIGHTNESS,BADGE_SETTINGS_THEME,BADGE_SETTINGS_SCREEN_TIMEOUT};
    for(unsigned i=0;i<sizeof(submenus)/sizeof(submenus[0]);++i) {
        s.settings=submenus[i];s.focus=5;
        action=badge_ui_state_key(&s,BADGE_KEY_OK,true,&data,&config);
        assert(action.kind==BADGE_ACTION_NONE&&s.settings==BADGE_SETTINGS_CLOSED);
    }
    config.username[0]=0;s.page=0;
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.modal==BADGE_MODAL_NONE);
    s=(badge_ui_state_t){.page=255,.focus=255,.language=255,.pr_tab=255,.star_tab=255,
                        .settings=(badge_settings_t)-1,.modal=(badge_modal_t)-1};
    data.scope_count=255;
    badge_ui_state_clamp(&s,&data);
    assert(s.page==0&&s.focus==0&&s.language==0&&s.pr_tab==0&&s.star_tab==0);
    assert(s.settings==BADGE_SETTINGS_CLOSED&&s.modal==BADGE_MODAL_NONE);
    badge_ui_state_t before=s;
    action=badge_ui_state_key(&s,(badge_key_t)-1,false,&data,&config);
    assert(action.kind==BADGE_ACTION_NONE&&!memcmp(&s,&before,sizeof(s)));
    s.page=2;badge_ui_state_key(&s,BADGE_KEY_UP,false,&data,&config);
    assert(s.language==BADGE_SCOPES-1); /* Never index beyond the fixed array. */
    s.settings=BADGE_SETTINGS_MAIN;s.focus=2;config.theme=255;
    badge_ui_state_key(&s,BADGE_KEY_OK,false,&data,&config);
    assert(s.settings==BADGE_SETTINGS_THEME&&s.focus==0);
    puts("badge UI navigation tests passed");
    return 0;
}
