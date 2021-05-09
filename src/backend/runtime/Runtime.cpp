#include "Runtime.h"
#include "Camera.h"
#include "Logger.h"
#include "driver/Configuration.h"
#include "loader/Parser.h"

namespace IG {

static inline void setup_technique(RuntimeRenderSettings& settings, LoaderOptions& lopts, const RuntimeOptions& opts)
{
	std::string tech_type;
	if (opts.OverrideTechnique.empty()) {
		const auto technique = lopts.Scene.technique();
		if (technique) {
			tech_type			   = technique->pluginType();
			settings.MaxPathLength = technique->property("max_depth").getInteger(settings.MaxPathLength);
		} else
			tech_type = "path";
	} else {
		tech_type = opts.OverrideTechnique;
	}

	if (tech_type == "debug")
		lopts.Configuration |= IG_C_RENDERER_DEBUG;
	else
		lopts.Configuration |= IG_C_RENDERER_PATH;
}

static inline void setup_film(RuntimeRenderSettings& settings, const LoaderOptions& lopts, const RuntimeOptions& opts)
{
	if (opts.OverrideFilmSize.has_value()) {
		settings.FilmWidth	= opts.OverrideFilmSize.value().first;
		settings.FilmHeight = opts.OverrideFilmSize.value().second;
	} else {
		const auto film = lopts.Scene.film();
		if (film) {
			const auto filmSize = film->property("size").getVector2(Vector2f(settings.FilmWidth, settings.FilmHeight));
			settings.FilmWidth	= filmSize.x();
			settings.FilmHeight = filmSize.y();
		}
	}
}

static inline void setup_camera(RuntimeRenderSettings& settings, LoaderOptions& lopts, const RuntimeOptions& opts)
{
	// Extract camera type first
	std::string camera_type = "perspective";
	if (!opts.OverrideCamera.empty()) {
		camera_type = opts.OverrideCamera;
	} else {
		const auto camera = lopts.Scene.camera();
		if (camera)
			camera_type = camera->pluginType();
	}

	if (camera_type == "orthogonal")
		lopts.Configuration |= IG_C_CAMERA_ORTHOGONAL;
	else if (camera_type == "fishlens")
		lopts.Configuration |= IG_C_CAMERA_FISHLENS;
	else if (camera_type == "list")
		lopts.Configuration |= IG_C_CAMERA_LIST;
	else
		lopts.Configuration |= IG_C_CAMERA_PERSPECTIVE;

	// Get initial location
	Transformf cameraTransform;
	if (opts.OverrideCameraSettings.has_value()) {
		cameraTransform = opts.OverrideCameraSettings.value().first;
		settings.FOV	= opts.OverrideCameraSettings.value().second;
	} else {
		const auto camera = lopts.Scene.camera();
		if (camera) {
			cameraTransform = camera->property("transform").getTransform();
			settings.FOV	= camera->property("fov").getNumber(settings.FOV);
		}
	}

	settings.CameraEye = cameraTransform * Vector3f::Zero();
	settings.CameraDir = cameraTransform.linear().col(2);
	settings.CameraUp  = cameraTransform.linear().col(1);
}

Runtime::Runtime(const std::filesystem::path& path, const RuntimeOptions& opts)
	: mInit(false)
	, mDevice(opts.Device)
	, mIteration(0)
{
	if (!mManager.init())
		throw std::runtime_error("Could not init modules!");

	LoaderOptions lopts;
	lopts.FilePath		= path;
	lopts.Configuration = targetToConfiguration(opts.DesiredTarget);

	// Parse scene file
	Parser::SceneParser parser;
	bool ok		= false;
	lopts.Scene = parser.loadFromFile(path, ok);
	if (!ok)
		throw std::runtime_error("Could not parse scene!");

	// Extract technique
	setup_technique(mLoadedRenderSettings, lopts, opts);

	// Extract film
	setup_film(mLoadedRenderSettings, lopts, opts);

	// Extract camera
	setup_camera(mLoadedRenderSettings, lopts, opts);

	// Check configuration
	const uint64 newConfig = mManager.checkConfiguration(lopts.Configuration);
	if (newConfig != lopts.Configuration) {
		IG_LOG(L_WARNING) << "Switched from "
						  << configurationToString(lopts.Configuration) << " to "
						  << configurationToString(newConfig) << std::endl;
	}
	lopts.Configuration = newConfig;

	LoaderResult result;
	if (!Loader::load(lopts, result))
		throw std::runtime_error("Could not load scene!");
	mDatabase = std::move(result.Database);

	IG_LOG(L_INFO) << "Loading configuration " << configurationToString(newConfig) << std::endl;
	if (!mManager.load(newConfig, mLoadedInterface))
		throw std::runtime_error("Error loading interface!");
}

Runtime::~Runtime()
{
	if (mInit)
		shutdown();
}

void Runtime::step(const Camera& camera)
{
	if (!mInit)
		return;

	if (mLoadedInterface.Configuration & IG_C_CAMERA_LIST) {
		IG_LOG(L_ERROR) << "Trying to use step() in a trace driver!" << std::endl;
		return;
	}

	DriverRenderSettings settings;
	for (int i = 0; i < 3; ++i)
		settings.eye[i] = camera.eye(i);
	for (int i = 0; i < 3; ++i)
		settings.dir[i] = camera.dir(i);
	for (int i = 0; i < 3; ++i)
		settings.up[i] = camera.up(i);
	for (int i = 0; i < 3; ++i)
		settings.right[i] = camera.right(i);
	settings.width			 = camera.w;
	settings.height			 = camera.h;
	settings.rays			 = nullptr; // No artifical ray streams
	settings.device			 = mDevice;
	settings.max_path_length = mLoadedRenderSettings.MaxPathLength;

	mLoadedInterface.RenderFunction(&settings, mIteration++);
}

void Runtime::trace(const std::vector<Ray>& rays, std::vector<float>& data)
{
	if (!mInit)
		return;

	if (!(mLoadedInterface.Configuration & IG_C_CAMERA_LIST)) {
		IG_LOG(L_ERROR) << "Trying to use trace() in a camera driver!" << std::endl;
		return;
	}

	DriverRenderSettings settings;
	settings.width			 = rays.size();
	settings.height			 = 1;
	settings.rays			 = rays.data();
	settings.device			 = mDevice;
	settings.max_path_length = mLoadedRenderSettings.MaxPathLength;

	mLoadedInterface.RenderFunction(&settings, mIteration++);

	// Get result
	const float* data_ptr = getFramebuffer(0);
	data.resize(rays.size() * 3);
	std::memcpy(data.data(), data_ptr, sizeof(float) * rays.size() * 3);
}

const float* Runtime::getFramebuffer(int aov) const
{
	return mLoadedInterface.GetFramebufferFunction(aov);
}

void Runtime::clearFramebuffer(int aov)
{
	return mLoadedInterface.ClearFramebufferFunction(aov);
}

void Runtime::setup(uint32 framebuffer_width, uint32 framebuffer_height)
{
	DriverSetupSettings settings;
	settings.database			= &mDatabase;
	settings.framebuffer_width	= std::max(1u, framebuffer_width);
	settings.framebuffer_height = std::max(1u, framebuffer_height);
	mLoadedInterface.SetupFunction(&settings);
	mInit = true;

	clearFramebuffer();
}

void Runtime::shutdown()
{
	mLoadedInterface.ShutdownFunction();
}

} // namespace IG