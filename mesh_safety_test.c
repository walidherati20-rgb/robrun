#include <stdio.h>
#include "rr_mesh_safety.h"
static int failures;
#define CHECK(c, label) do { if (!(c)) { ++failures; printf("FAIL: %s\n", label); } } while (0)
int main(void) {
    int subset[3] = {-1, 10, 11}, seed = 12;
    CHECK(rr_mesh_span_covers(subset, 10, 3, &seed, 1), "complete cached span accepted");
    subset[0] = 2;
    CHECK(!rr_mesh_span_covers(subset, 10, 3, &seed, 1),
          "new seat ancestor outside cached span triggers full read");
    int all[13] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,2,10,11};
    CHECK(rr_mesh_span_covers(all, 0, 13, &seed, 1),
          "full hierarchy includes new live vehicle ancestor");
    all[2] = 12;
    CHECK(!rr_mesh_span_covers(all, 0, 13, &seed, 1), "cycle rejected");
    all[2] = -2;
    CHECK(!rr_mesh_span_covers(all, 0, 13, &seed, 1), "broken parent rejected");
    seed = -1;
    CHECK(rr_mesh_span_covers(subset, 10, 3, &seed, 1), "absent optional joint ignored");
    float model[3] = {0,0,0};
    float anchors[5][3] = {{3.5f,1.5f,0},{3.5f,1.2f,0},{3.5f,0.9f,0},
                           {3.5f,0.6f,0},{3.5f,0,0}};
    CHECK(rr_mesh_anchors_valid(anchors, model, 1),
          "coherent seated rig with lagging model accepted");
    CHECK(!rr_mesh_anchors_valid(anchors, model, 0),
          "standing model association remains strict");
    anchors[0][1] = 9;
    CHECK(!rr_mesh_anchors_valid(anchors, model, 1), "stretched/torn body rejected");
    anchors[0][1] = NAN;
    CHECK(!rr_mesh_anchors_valid(anchors, model, 1), "nonfinite body rejected");
    anchors[0][1] = 1.5f;
    model[0] = -100;
    CHECK(!rr_mesh_anchors_valid(anchors, model, 1), "foreign rig association rejected");
    printf("MESH SAFETY TEST %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures != 0;
}
