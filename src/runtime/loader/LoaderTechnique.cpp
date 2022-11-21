#include "LoaderTechnique.h"
#include "Loader.h"
#include "LoaderLight.h"
#include "LoaderUtils.h"
#include "Logger.h"
#include "ShadingTree.h"
#include "light/LightHierarchy.h"
#include "serialization/VectorSerializer.h"
#include "shader/RayGenerationShader.h"
#include "shader/ShaderUtils.h"

#include <numeric>
#include <string>

namespace IG {
// FIXME: The convergence rate is kinda bad and some have a slight bias, keep it uniform until fixes
static const std::string DefaultLightSelector = "uniform";

static TechniqueInfo technique_empty_get_info(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&)
{
    return {};
}

/////////////////////////

static void ao_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>&, LoaderContext&)
{
    stream << "  let technique = make_ao_renderer();" << std::endl;
}

/////////////////////////

static void debug_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>&, LoaderContext&)
{
    // TODO: Maybe add a changeable default mode?
    stream << "  let debug_mode = registry::get_global_parameter_i32(\"__debug_mode\", 0);" << std::endl
           << "  let technique  = make_debug_renderer(debug_mode);" << std::endl;
}

static TechniqueInfo debug_get_info(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&)
{
    TechniqueInfo info;
    info.Variants[0].UsesLights = true; // We make use of the emissive information!
    return info;
}

/////////////////////////

static void wireframe_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>&, LoaderContext&)
{
    // Camera was defined by RequiresExplicitCamera flag
    stream << "  let technique = make_wireframe_renderer(camera, settings.width, settings.height);" << std::endl;
}

static TechniqueInfo wireframe_get_info(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&)
{
    TechniqueInfo info;
    info.Variants[0].RequiresExplicitCamera    = true; // We make use of the camera differential!
    info.Variants[0].PrimaryPayloadCount       = 2;
    info.Variants[0].EmitterPayloadInitializer = "make_simple_payload_initializer(init_wireframe_raypayload)";
    return info;
}

/////////////////////////

static void lv_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    const int max_depth  = technique ? technique->property("max_depth").getInteger(64) : 64;
    const std::string ls = technique ? technique->property("light_selector").getString(DefaultLightSelector) : DefaultLightSelector;
    const float factor   = technique ? technique->property("no_connection_factor").getNumber(0.0f) : 0.0f; // Set to zero to disable contribution of points not connected to a light

    ShadingTree tree(ctx);
    stream << ctx.Lights->generateLightSelector(ls, tree);

    stream << "  let technique = make_lv_renderer(" << max_depth << ", light_selector, " << factor << ");" << std::endl;
}

static TechniqueInfo lv_get_info(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&)
{
    TechniqueInfo info;
    info.Variants[0].UsesLights                = true;
    info.Variants[0].PrimaryPayloadCount       = 1;
    info.Variants[0].SecondaryPayloadCount     = 1;
    info.Variants[0].ShadowHandlingMode        = ShadowHandlingMode::Advanced;
    info.Variants[0].EmitterPayloadInitializer = "make_simple_payload_initializer(init_lv_raypayload)";
    return info;
}

/////////////////////////

static void enable_ib(TechniqueInfo& info, bool always = false, bool extend = true)
{
    info.EnabledAOVs.emplace_back("Normals");
    info.EnabledAOVs.emplace_back("Albedo");
    info.EnabledAOVs.emplace_back("Depth");

    if (extend)
        info.Variants.emplace_back();

    info.Variants.back().LockFramebuffer           = true;
    info.Variants.back().OverrideSPI               = 1;
    info.Variants.back().PrimaryPayloadCount       = 2;
    info.Variants.back().EmitterPayloadInitializer = "make_simple_payload_initializer(init_ib_raypayload)";

    const size_t variantCount = info.Variants.size();
    if (variantCount > 1) {
        if (info.VariantSelector) {
            const auto prevSelector = info.VariantSelector;
            info.VariantSelector    = [=](size_t iter) {
                if (always || iter == 0) {
                    std::vector<size_t> pass_with_ib = prevSelector(iter);
                    pass_with_ib.emplace_back(variantCount - 1);
                    return pass_with_ib;
                } else {
                    return prevSelector(iter);
                }
            };
        } else {
            info.VariantSelector = [=](size_t iter) {
                if (always || iter == 0) {
                    std::vector<size_t> pass_with_ib(variantCount);
                    std::iota(std::begin(pass_with_ib), std::end(pass_with_ib), 0);
                    return pass_with_ib;
                } else {
                    std::vector<size_t> pass_without_ib(variantCount - 1);
                    std::iota(std::begin(pass_without_ib), std::end(pass_without_ib), 0);
                    return pass_without_ib;
                }
            };
        }
    }
}

static bool handle_ib_body(std::ostream& stream, const std::shared_ptr<Parser::Object>& technique, const LoaderContext& ctx)
{
    const auto& info = ctx.TechniqueInfo;

    const auto& normal_it = std::find(info.EnabledAOVs.begin(), info.EnabledAOVs.end(), "Normals");
    if (normal_it == info.EnabledAOVs.end())
        return false;

    if (ctx.CurrentTechniqueVariant != info.Variants.size() - 1)
        return false;

    const int max_depth        = technique ? technique->property("max_depth").getInteger(64) : 64;
    const bool handle_specular = ctx.Denoiser.FollowSpecular || (technique ? technique->property("denoiser_handle_specular").getBool(false) : false);

    stream << "  let aov_normals = device.load_aov_image(\"Normals\", spi); aov_normals.mark_as_used();" << std::endl;
    stream << "  let aov_albedo = device.load_aov_image(\"Albedo\", spi); aov_albedo.mark_as_used();" << std::endl;
    stream << "  let aov_depth  = device.load_aov_image(\"Depth\", spi); aov_depth.mark_as_used();" << std::endl;

    stream << "  let aovs = @|id:i32| -> AOVImage {" << std::endl
           << "    match(id) {" << std::endl
           << "      0x1000 => aov_normals," << std::endl
           << "      0x1001 => aov_albedo," << std::endl
           << "      0x1002 => aov_depth," << std::endl
           << "      _ => make_empty_aov_image()" << std::endl
           << "    }" << std::endl
           << "  };" << std::endl;

    stream << "  let technique = make_infobuffer_renderer(" << max_depth << ", aovs, " << (handle_specular ? "true" : "false") << ");" << std::endl;

    return true;
}

static TechniqueInfo ib_get_info(const std::string&, const std::shared_ptr<Parser::Object>& technique, const LoaderContext& ctx)
{
    const bool apply_always = !ctx.Denoiser.OnlyFirstIteration || (technique ? technique->property("denoiser_ib_all_iterations").getBool(false) : false);

    TechniqueInfo info;
    enable_ib(info, apply_always, false);
    return info;
}

static void ib_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    handle_ib_body(stream, technique, ctx);
}

/////////////////////////

static TechniqueInfo path_get_info(const std::string&, const std::shared_ptr<Parser::Object>& technique, const LoaderContext& ctx)
{
    TechniqueInfo info;

    // Check if we have a proper defined technique
    // It is totally fine to only define the type by other means then the scene config
    if (technique) {
        if (technique->property("aov_mis").getBool(false)) {
            info.EnabledAOVs.emplace_back("Direct Weights");
            info.EnabledAOVs.emplace_back("NEE Weights");
            info.Variants[0].ShadowHandlingMode = ShadowHandlingMode::Advanced;
        }
    }

    info.Variants[0].UsesLights                = true;
    info.Variants[0].PrimaryPayloadCount       = 6;
    info.Variants[0].EmitterPayloadInitializer = "make_simple_payload_initializer(init_pt_raypayload)";

    if (ctx.Denoiser.Enabled)
        enable_ib(info, !ctx.Denoiser.OnlyFirstIteration);

    return info;
}

static void path_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    if (handle_ib_body(stream, technique, ctx))
        return;

    const int max_depth     = technique ? technique->property("max_depth").getInteger(64) : 64;
    const float clamp_value = technique ? technique->property("clamp").getNumber(0) : 0; // Allow clamping of contributions
    const std::string ls    = technique ? technique->property("light_selector").getString(DefaultLightSelector) : DefaultLightSelector;
    const bool hasMISAOV    = technique ? technique->property("aov_mis").getBool(false) : false;

    if (hasMISAOV) {
        stream << "  let aov_di  = device.load_aov_image(\"Direct Weights\", spi); aov_di.mark_as_used();" << std::endl;
        stream << "  let aov_nee = device.load_aov_image(\"NEE Weights\", spi); aov_nee.mark_as_used();" << std::endl;
    }

    stream << "  let aovs = @|id:i32| -> AOVImage {" << std::endl
           << "    match(id) {" << std::endl;

    if (hasMISAOV) {
        stream << "      1 => aov_di," << std::endl
               << "      2 => aov_nee," << std::endl;
    }

    stream << "      _ => make_empty_aov_image()" << std::endl
           << "    }" << std::endl
           << "  };" << std::endl;

    ShadingTree tree(ctx);
    stream << ctx.Lights->generateLightSelector(ls, tree);

    stream << "  let technique = make_path_renderer(" << max_depth << ", light_selector, aovs, " << clamp_value << ");" << std::endl;
}

/////////////////////////

static TechniqueInfo volpath_get_info(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext& ctx)
{
    TechniqueInfo info;
    info.Variants[0].UsesLights                = true;
    info.Variants[0].UsesMedia                 = true;
    info.Variants[0].PrimaryPayloadCount       = 7;
    info.Variants[0].EmitterPayloadInitializer = "make_simple_payload_initializer(init_vpt_raypayload)";

    if (ctx.Denoiser.Enabled)
        enable_ib(info, !ctx.Denoiser.OnlyFirstIteration);

    return info;
}

static void volpath_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    if (handle_ib_body(stream, technique, ctx))
        return;

    const int max_depth     = technique ? technique->property("max_depth").getInteger(64) : 64;
    const float clamp_value = technique ? technique->property("clamp").getNumber(0) : 0; // Allow clamping of contributions
    const std::string ls    = technique ? technique->property("light_selector").getString(DefaultLightSelector) : DefaultLightSelector;

    ShadingTree tree(ctx);
    stream << ctx.Lights->generateLightSelector(ls, tree);

    stream << "  let aovs = @|_id:i32| make_empty_aov_image();" << std::endl;
    stream << "  let technique = make_volume_path_renderer(" << max_depth << ", light_selector, media, aovs, " << clamp_value << ");" << std::endl;
}

/////////////////////////////////

static TechniqueInfo bi_get_info(const std::string&, const std::shared_ptr<Parser::Object>& technique, const LoaderContext& ctx)
{
    TechniqueInfo info;

    if (technique->property("aov_bi").getBool(false)) {
        for(int i = 0; i < 4; i++){
            info.EnabledAOVs.emplace_back("s=" + std::to_string(i));
        }
    }

    info.Variants.resize(2);

    //You should swap light and pathtracer than this would not be needed!!
    auto variant_selector = [] (size_t size){
        std::vector<size_t> ret(2);
        ret.at(0) = 1;
        ret.at(1) = 0;
        return ret;
    };

    info.VariantSelector = variant_selector;

    auto light_camera = [] (LoaderContext& ctx){
        std::stringstream stream;
        
        stream << RayGenerationShader::begin(ctx) << std::endl;
        stream << ShaderUtils::generateDatabase(ctx) << std::endl;

        ShadingTree tree(ctx);
        stream << ctx.Lights->generate(tree, false) << std::endl;
        stream << ctx.Lights->generateLightSelector("", tree);

        stream << "  let film_width = settings.width;" << std::endl;
        stream << "  let film_height = settings.height;" << std::endl;
        stream << "  let spi = " << ShaderUtils::inlineSPI(ctx) << ";" << std::endl;
        stream << "  let spp = " << ctx.SamplesPerIteration << " : i32;" << std::endl;
        //The Buffer Size is far too big!!
        stream << "  let max_depth_light = 5;" << std::endl;
        stream << "  let buf_size = film_width * film_height * max_depth_light * vertex_size;" << std::endl; //TODO Find a better to set a max depth
        stream << "  let buf = device.request_buffer(\"bi\", buf_size, 0);" << std::endl;
        stream << "  let offset:f32 = 0.001;" << std::endl; //TODO find a better way to define this
        stream << "  let camera = make_light_camera(" << std::endl;
        stream << "     offset," << std::endl;
        stream << "     flt_max," << std::endl;
        stream << "     film_width as f32," << std::endl;
        stream << "     film_height as f32," << std::endl;
        stream << "     buf," << std::endl;
        stream << "     light_selector,"  << std::endl;
        stream << "     max_depth_light);" << std::endl;//TODO Find a better to set a max depth

        std::string pixel_sampler = "make_uniform_pixel_sampler()";
        if (ctx.PixelSamplerType == "halton") {
            stream << "  let halton_setup = setup_halton_pixel_sampler(device, settings.width, settings.height, settings.iter, xmin, ymin, xmax, ymax);" << std::endl;
            pixel_sampler = "make_halton_pixel_sampler(halton_setup)";
        } else if (ctx.PixelSamplerType == "mjitt") {
            pixel_sampler = "make_mjitt_pixel_sampler(4, 4)";
        }

        stream << "  let emitter = make_camera_emitter(camera, settings.iter, spi, " << pixel_sampler << ", " << ctx.CurrentTechniqueVariantInfo().GetEmitterPayloadInitializer() << ");" << std::endl;
        // stream << "  let emitter = make_camera_emitter(camera, settings.iter, spp, make_uniform_pixel_sampler()/*make_mjitt_pixel_sampler(4,4)*/, init_raypayload);" << std::endl;
        
        stream << RayGenerationShader::end() << std::endl;

        return stream.str();
    };


    info.Variants[1].OverrideCameraGenerator = light_camera;
    info.Variants[1].LockFramebuffer = true;
    info.Variants[1].EmitterPayloadInitializer = "make_simple_payload_initializer(init_biraypayload)";
    info.Variants[1].PrimaryPayloadCount = 8;

    info.Variants[0].RequiresExplicitCamera    = true; 
    info.Variants[0].UsesLights = true;
    info.Variants[0].EmitterPayloadInitializer = "make_simple_payload_initializer(init_biraypayload)";
    info.Variants[0].ShadowHandlingMode = ShadowHandlingMode::AdvancedWithMaterials;
    info.Variants[0].PrimaryPayloadCount = 8;
    info.Variants[0].SecondaryPayloadCount = 12; // Warum 12, nur 10 werden genutzt?

    // if (ctx.Denoiser.Enabled)
    //     enable_ib(info, !ctx.Denoiser.OnlyFirstIteration);

    return info;
}

static void bi_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    if (handle_ib_body(stream, technique, ctx))
        return;

    stream << "  let film_width = settings.width;" << std::endl;
    stream << "  let film_height = settings.height;" << std::endl;
    stream << "  let max_depth_light = 5;" << std::endl;
    stream << "  let buf_size = film_width * film_height * max_depth_light * vertex_size;" << std::endl;
    stream << "  let buf = device.request_buffer(\"bi\", buf_size, 0);" << std::endl;

    const int max_depth     = technique ? technique->property("max_depth").getInteger(64) : 64;
    const float clamp_value = technique ? technique->property("clamp").getNumber(0) : 0; // Allow clamping of contributions
    const std::string ls    = technique ? technique->property("light_selector").getString(DefaultLightSelector) : DefaultLightSelector;
    const bool hasMISAOV    = technique ? technique->property("aov_bi").getBool(false) : false;


    if(ctx.CurrentTechniqueVariant == 0){
        if (hasMISAOV) {
            for(int i = 0; i < 4; i++){
                stream << "  let aov_" << i << "  = device.load_aov_image(\"s=" << i << "\", spi); aov_" << i << ".mark_as_used();" << std::endl;
            }
        }

        stream << "  let aovs = @|id:i32| -> AOVImage {" << std::endl
            << "    match(id) {" << std::endl;

        if (hasMISAOV) {
            for(int i = 0; i < 4; i++){
                //we use the ids => 2 because the framebuffer will always have the id = 1
                stream << "      " << i + 2 << " => aov_" << i << "," << std::endl;
            }
        }

        stream << "      _ => make_empty_aov_image()" << std::endl
            << "    }" << std::endl
            << "  };" << std::endl;

        stream << "  let buf_size_camera = film_width * film_height * " << max_depth << " * vertex_size;" << std::endl;
        stream << "  let buf_camera = device.request_buffer(\"camera\", buf_size_camera, 0);" << std::endl;
        size_t counter = 1;

        ShadingTree tree(ctx);
        stream << ctx.Lights->generateLightSelector(ls, tree);
        stream << "  let technique = make_bi_renderer(" << max_depth << ", light_selector, aovs, buf, buf_camera, max_depth_light, camera, film_width, film_height);" << std::endl;
    }else{
        stream << "  let technique = make_light_renderer(buf, max_depth_light);" << std::endl;
    }
}

/////////////////////////////////

static std::string ppm_light_camera_generator(LoaderContext& ctx)
{
    std::stringstream stream;

    stream << RayGenerationShader::begin(ctx) << std::endl;

    stream << ShaderUtils::generateDatabase(ctx) << std::endl;

    ShadingTree tree(ctx);
    stream << ctx.Lights->generate(tree, false) << std::endl;
    stream << ctx.Lights->generateLightSelector("", tree);

    stream << "  let spi = " << ShaderUtils::inlineSPI(ctx) << ";" << std::endl;
    stream << "  let emitter = make_ppm_light_emitter(light_selector, settings.iter);" << std::endl;

    stream << RayGenerationShader::end();

    return stream.str();
}

static std::string ppm_before_iteration_generator(LoaderContext& ctx)
{
    std::stringstream stream;

    const auto technique     = ctx.Scene.technique();
    const size_t max_photons = std::max(100, technique ? technique->property("photons").getInteger(1000000) : 1000000);

    stream << ShaderUtils::beginCallback(ctx) << std::endl
           << "  let scene_bbox = " << LoaderUtils::inlineSceneBBox(ctx) << ";" << std::endl
           << "  ppm_handle_before_iteration(device, iter, " << ctx.CurrentTechniqueVariant << ", " << max_photons << ", scene_bbox);" << std::endl
           << ShaderUtils::endCallback() << std::endl;

    return stream.str();
}

static TechniqueInfo ppm_get_info(const std::string&, const std::shared_ptr<Parser::Object>& technique, const LoaderContext& ctx)
{
    TechniqueInfo info;

    // We got two passes. (0 -> Light Tracer, 1 -> Path Tracer with merging)
    info.Variants.resize(2);
    info.Variants[0].UsesLights = false; // LT makes no use of other lights (but starts on one)
    info.Variants[1].UsesLights = true;  // Standard PT still use of lights in the miss shader

    info.Variants[0].PrimaryPayloadCount = 7;
    info.Variants[1].PrimaryPayloadCount = 7;

    info.Variants[1].EmitterPayloadInitializer = "make_simple_payload_initializer(init_ppm_raypayload)";

    // To start from a light source, we do have to override the standard camera generator for LT
    info.Variants[0].OverrideCameraGenerator = ppm_light_camera_generator;

    // Each pass makes use of pre-iteration setups
    info.Variants[0].CallbackGenerators[(int)CallbackType::BeforeIteration] = ppm_before_iteration_generator; // Reset light cache
    info.Variants[1].CallbackGenerators[(int)CallbackType::BeforeIteration] = ppm_before_iteration_generator; // Construct query structure

    // The LT works independent of the framebuffer and requires a different work size
    const int max_photons           = technique ? technique->property("photons").getInteger(1000000) : 1000000;
    info.Variants[0].OverrideWidth  = max_photons; // Photon count
    info.Variants[0].OverrideHeight = 1;
    info.Variants[0].OverrideSPI    = 1; // The light tracer variant is always just one spi (Could be improved in the future though)

    info.Variants[0].LockFramebuffer = true; // We do not change the framebuffer

    // Check if we have a proper defined technique
    // It is totally fine to only define the type by other means then the scene config
    if (technique) {
        if (technique->property("aov").getBool(false)) {
            info.EnabledAOVs.emplace_back("Direct Weights");
            info.EnabledAOVs.emplace_back("Merging Weights");
        }
    }

    if (ctx.Denoiser.Enabled)
        enable_ib(info, !ctx.Denoiser.OnlyFirstIteration);

    return info;
}

static void ppm_body_loader(std::ostream& stream, const std::string&, const std::shared_ptr<Parser::Object>& technique, LoaderContext& ctx)
{
    if (handle_ib_body(stream, technique, ctx))
        return;

    const size_t max_photons = std::max(100, technique ? technique->property("photons").getInteger(1000000) : 1000000);
    const int max_depth      = technique ? technique->property("max_depth").getInteger(8) : 8;
    const float radius       = technique ? technique->property("radius").getNumber(0.01f) : 0.01f;
    const float clamp_value  = technique ? technique->property("clamp").getNumber(0) : 0; // Allow clamping of contributions

    bool is_lighttracer = ctx.CurrentTechniqueVariant == 0;

    if (is_lighttracer) {
        stream << "  let aovs = @|id:i32| -> AOVImage {" << std::endl
               << "    match(id) {" << std::endl
               << "      _ => make_empty_aov_image()" << std::endl
               << "    }" << std::endl
               << "  };" << std::endl;
    } else {
        const bool hasAOV = technique ? technique->property("aov").getBool(false) : false;

        if (hasAOV) {
            stream << "  let aov_di   = device.load_aov_image(\"Direct Weights\", spi); aov_di.mark_as_used();" << std::endl;
            stream << "  let aov_merg = device.load_aov_image(\"Merging Weights\", spi); aov_merg.mark_as_used();" << std::endl;
        }

        stream << "  let aovs = @|id:i32| -> AOVImage {" << std::endl
               << "    match(id) {" << std::endl;

        if (hasAOV) {
            stream << "      1 => aov_di," << std::endl
                   << "      2 => aov_merg," << std::endl;
        }

        stream << "      _ => make_empty_aov_image()" << std::endl
               << "    }" << std::endl
               << "  };" << std::endl;

        stream << "  let ppm_radius = ppm_compute_radius(" << radius * ctx.Environment.SceneDiameter << ", settings.iter);" << std::endl;
    }

    stream << "  let scene_bbox  = " << LoaderUtils::inlineSceneBBox(ctx) << ";" << std::endl
           << "  let light_cache = make_ppm_lightcache(device, " << max_photons << ", scene_bbox);" << std::endl;

    if (is_lighttracer) {
        stream << "  let technique = make_ppm_light_renderer(" << max_depth << ", aovs, light_cache);" << std::endl;
    } else {
        ShadingTree tree(ctx);
        stream << ctx.Lights->generateLightSelector("", tree);
        stream << "  let technique = make_ppm_path_renderer(" << max_depth << ",light_selector, ppm_radius, aovs, " << clamp_value << ", light_cache);" << std::endl;
    }
}

/////////////////////////////////

// Will return information about the enabled AOVs
using TechniqueGetInfo = TechniqueInfo (*)(const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&);

// Every body loader has to define 'technique'
using TechniqueBodyLoader = void (*)(std::ostream&, const std::string&, const std::shared_ptr<Parser::Object>&, LoaderContext&);

// Every header loader has to define 'RayPayloadComponents' and 'init_raypayload()'
using TechniqueHeaderLoader = void (*)(std::ostream&, const std::string&, const std::shared_ptr<Parser::Object>&, const LoaderContext&);

static const struct TechniqueEntry {
    const char* Name;
    TechniqueGetInfo GetInfo;
    TechniqueBodyLoader BodyLoader;
} _generators[] = {
    { "ao", technique_empty_get_info, ao_body_loader },
    { "bi", bi_get_info, bi_body_loader },
    { "path", path_get_info, path_body_loader },
    { "volpath", volpath_get_info, volpath_body_loader },
    { "debug", debug_get_info, debug_body_loader },
    { "ppm", ppm_get_info, ppm_body_loader },
    { "photonmapper", ppm_get_info, ppm_body_loader },
    { "wireframe", wireframe_get_info, wireframe_body_loader },
    { "infobuffer", ib_get_info, ib_body_loader },
    { "lightvisibility", lv_get_info, lv_body_loader },
    { "", nullptr, nullptr }
};

static const TechniqueEntry* getTechniqueEntry(const std::string& name)
{
    const std::string lower_name = to_lowercase(name);
    for (size_t i = 0; _generators[i].BodyLoader; ++i) {
        if (_generators[i].Name == lower_name)
            return &_generators[i];
    }
    IG_LOG(L_ERROR) << "No technique type '" << name << "' available" << std::endl;
    return nullptr;
}

std::optional<TechniqueInfo> LoaderTechnique::getInfo(const LoaderContext& ctx)
{
    const auto* entry = getTechniqueEntry(ctx.TechniqueType);
    if (!entry)
        return {};

    const auto technique = ctx.Scene.technique();

    return entry->GetInfo(ctx.TechniqueType, technique, ctx);
}

std::string LoaderTechnique::generate(LoaderContext& ctx)
{
    const auto* entry = getTechniqueEntry(ctx.TechniqueType);
    if (!entry)
        return {};

    const auto technique = ctx.Scene.technique();

    std::stringstream stream;
    entry->BodyLoader(stream, ctx.TechniqueType, technique, ctx);

    return stream.str();
}

std::vector<std::string> LoaderTechnique::getAvailableTypes()
{
    std::vector<std::string> array;

    for (size_t i = 0; _generators[i].BodyLoader; ++i)
        array.emplace_back(_generators[i].Name);

    std::sort(array.begin(), array.end());
    return array;
}
} // namespace IG