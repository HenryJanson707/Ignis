#include "RayGenerationShader.h"
#include "Logger.h"
#include "loader/Loader.h"
#include "loader/LoaderTechnique.h"
#include "loader/LoaderLight.h"
#include "loader/ShaderUtils.h"
#include "loader/ShadingTree.h"

#include <sstream>

namespace IG {
using namespace Parser;

std::string RayGenerationShader::setup(LoaderContext& ctx)
{
    std::stringstream stream;

    stream << LoaderTechnique::generateHeader(ctx, true) << std::endl;

    stream << "#[export] fn ig_ray_generation_shader(settings: &Settings, iter: i32, id: &mut i32, size: i32, xmin: i32, ymin: i32, xmax: i32, ymax: i32) -> i32 {" << std::endl;
    stream << "  " << ShaderUtils::constructDevice(ctx.Target) << std::endl;
    stream << std::endl;

    std::string gen;
    if (ctx.CameraType == "perspective")
        gen = "make_perspective_camera";
    else if (ctx.CameraType == "orthogonal")
        gen = "make_orthogonal_camera";
    else if (ctx.CameraType == "fishlens" || ctx.CameraType == "fisheye")
        gen = "make_fishlens_camera";
    else if (!(ctx.CameraType == "list" || ctx.CameraType == "light")) { //TODO this is a suboptimal way of doing the distinction
        IG_LOG(L_ERROR) << "Unknown camera type '" << ctx.CameraType << "'" << std::endl;
        return {};
    }

    if(ctx.CameraType == "light"){
        stream << ShaderUtils::generateDatabase() << std::endl;

        ShadingTree tree(ctx);

        stream << LoaderLight::generate(tree, false) << std::endl;
        stream << "  let (film_width, film_height) = device.get_film_size();" << std::endl;
        //The Buffer Size is far too big!!
        stream << "  let max_depth = 5;" << std::endl;
        stream << "  let buf_size = film_width * film_height * 4 * max_depth * 12;" << std::endl; //TODO Find a better to set a max depth
        stream << "  let buf = device.request_buffer(\"bi\", buf_size);" << std::endl;
        stream << "  let camera = make_light_camera(" << std::endl;
        stream << "     settings.tmin," << std::endl;
        stream << "     settings.tmax," << std::endl;
        stream << "     buf," << std::endl;
        stream << "     num_lights," << std::endl;
        stream << "     lights,"  << std::endl;
        stream << "     max_depth);" << std::endl;//TODO Find a better to set a max depth
    }

    if (!gen.empty()) {
        stream << "  let camera = " << gen << "(" << std::endl
               << "    settings.eye," << std::endl
               << "    make_mat3x3(settings.right, settings.up, settings.dir)," << std::endl
               << "    settings.width," << std::endl
               << "    settings.height," << std::endl
               << "    settings.tmin," << std::endl
               << "    settings.tmax" << std::endl
               << "  );" << std::endl
               << std::endl;
    }

    stream << "  let spp = " << ctx.SamplesPerIteration << " : i32;" << std::endl;
    if (ctx.CameraType == "list") {
        stream << "  let emitter = make_list_emitter(device.load_rays(), iter, init_raypayload);" << std::endl;
    } else {
        IG_ASSERT(!gen.empty(), "Generator function can not be empty!");
        stream << "  let emitter = make_camera_emitter(camera, iter, spp, make_uniform_pixel_sampler()/*make_mjitt_pixel_sampler(4,4)*/, init_raypayload);" << std::endl;
    }

    stream << "  device.generate_rays(emitter, id, size, xmin, ymin, xmax, ymax, spp)" << std::endl
           << "}" << std::endl;

    return stream.str();
}

} // namespace IG