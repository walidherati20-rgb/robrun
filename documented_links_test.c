/* Tests for the actual layout adapter with an in-memory graph. No OS access. */
#include "rr_documented_links.h"
#include <stdio.h>
#include <string.h>
enum { GAME=0x100000, MODEL=0x110000, BATTLE=0x120000, ENTITIES=0x130000,
       WORLD=0x140000, CAMERA=0x150000, PROCESSOR=0x160000, SCREEN=0x170000,
       ACTIVE=0x180000, UPDATER=0x190000, VIEWS=0x200000, AIM=0x210000,
       BRAIN=0x220000, OTHER_MODEL=0x230000 };
static struct { uint64_t address,value; } edges[64];
static struct { uint64_t address; const char* type; } types[16];
static int edge_count,type_count,changing_reads;
static uint64_t changing_address;
static void edge(uint64_t address,uint64_t value) {
    for(int i=0;i<edge_count;++i)if(edges[i].address==address){edges[i].value=value;return;}
    edges[edge_count].address=address;edges[edge_count++].value=value;
}
static void object(uint64_t address,const char* type) {
    for(int i=0;i<type_count;++i)if(types[i].address==address){types[i].type=type;return;}
    types[type_count].address=address;types[type_count++].type=type;
}
static uint64_t read_pointer(uint64_t address) {
    if(address==changing_address&&++changing_reads>1)return 0;
    for(int i=0;i<edge_count;++i)if(edges[i].address==address)return edges[i].value;
    return 0;
}
static int is_type(uint64_t address,const char* type) {
    for(int i=0;i<type_count;++i)if(types[i].address==address)return !strcmp(types[i].type,type);
    return 0;
}
static void reset(void) {
    edge_count=type_count=changing_reads=0;changing_address=0;
    object(GAME,"Game");object(MODEL,"GameModel");object(OTHER_MODEL,"GameModel");
    object(BATTLE,"BattleModel");object(ENTITIES,"BattleEntitiesModel");
    object(WORLD,"WorldEntitiesModel");object(CAMERA,"BattleCameraModel");
    object(PROCESSOR,"StartGameProcessor");object(SCREEN,"ScreenManager");
    object(ACTIVE,"BattleScreenUpdater");object(UPDATER,"BattleUpdater");
    object(VIEWS,"BattleEntitiesUpdater");object(AIM,"BattleAimTargetUpdater");object(BRAIN,"CinemachineBrain");
    edge(GAME+0xD0,MODEL);edge(MODEL+0x10,BATTLE);edge(BATTLE+0x90,ENTITIES);
    edge(ENTITIES+0x60,WORLD);edge(BATTLE+0x358,CAMERA);
    edge(GAME+0x98,PROCESSOR);edge(PROCESSOR+0x20,SCREEN);edge(SCREEN+0x50,ACTIVE);
    edge(ACTIVE+0x18,UPDATER);edge(UPDATER+0x90,VIEWS);edge(VIEWS+0x18,ENTITIES);
    edge(VIEWS+0x70,BATTLE);edge(UPDATER+0x1B8,AIM);edge(AIM+0x30,ENTITIES);edge(AIM+0x48,BRAIN);
}
int main(void) {
    int failures=0;RR_DOCUMENTED_LINKS result;
#define READ() rr_documented_scene(GAME,read_pointer,is_type,&result)
#define CHECK(c,label) do {int ok=!!(c);printf("[%s] %s\n",ok?"PASS":"FAIL",label);failures+=!ok;}while(0)
    reset();CHECK(READ()&&result.world==WORLD&&result.game_model_offset==0xD0,"documented D0 model chain validates");
    CHECK(result.views==VIEWS&&result.aim==AIM,"direct bone-view and aim updaters resolve without heap search");
    reset();edge(GAME+0xD0,0);edge(GAME+0xD8,MODEL);
    CHECK(READ()&&result.game_model_offset==0xD8,"legacy D8 model remains supported after type checks");
    reset();edge(GAME+0xD8,OTHER_MODEL);CHECK(!READ(),"conflicting valid model layouts rejected as ambiguous");
    reset();edge(GAME+0xD8,MODEL);CHECK(READ()&&result.game_model_offset==0xD0,"identical model aliases are not ambiguous");
    reset();object(GAME,"Wrong");CHECK(!READ(),"wrong Game type rejected");
    reset();object(MODEL,"Wrong");CHECK(!READ(),"wrong GameModel type rejected");
    reset();edge(MODEL+0x10,0);CHECK(!READ(),"hub/loading null BattleModel does not create a fake scene");
    reset();edge(VIEWS+0x18,999);CHECK(READ()&&!result.views,"views linked to another battle ignored");
    reset();edge(VIEWS+0x70,999);CHECK(READ()&&!result.views,"views linked to another model ignored");
    reset();edge(AIM+0x30,999);CHECK(READ()&&!result.aim,"foreign aim updater ignored");
    reset();object(BRAIN,"Wrong");CHECK(READ()&&!result.aim,"aim updater without valid camera brain ignored");
    reset();object(PROCESSOR,"Wrong");CHECK(READ()&&!result.views&&!result.aim,"missing optional updater chain keeps validated scene available");
    reset();edge(BATTLE+0x358,0);CHECK(READ()&&!result.camera,"missing camera is not replaced with an invented pointer");
    reset();changing_address=GAME+0xD0;CHECK(!READ(),"model pointer changing during the read rejects sample");
    reset();changing_address=ENTITIES+0x60;CHECK(!READ(),"world pointer changing during the read rejects sample");
    reset();edge(GAME+0xD0,0);CHECK(!READ(),"no recognized model layout rejected");
    reset();object(WORLD,"Wrong");CHECK(!READ(),"world class mismatch rejected");
    printf("Mock graph only; no game/backend accessed.\n");
    return failures?1:0;
}
