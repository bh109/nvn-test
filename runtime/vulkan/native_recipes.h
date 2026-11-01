#pragma once
#include "native_pipeline.h"
namespace nativevk {
struct RecipeProgram {uint64_t id=0;bool compute=false;Spirv first,second;};
struct RecipePipeline {uint64_t program=0;bool compute=false;GraphicsKey key={};};
uint64_t record_program(const std::string &directory,bool compute,const Spirv &first,const Spirv *second);
void record_pipeline(const std::string &directory,uint64_t program,const GraphicsState *state);
bool read_program_recipe(const std::string &path,RecipeProgram &result);
bool read_pipeline_recipe(const std::string &path,RecipePipeline &result);
std::string recipe_program_path(const std::string &directory,uint64_t id);
GraphicsState graphics_state(const GraphicsKey &key);
}
