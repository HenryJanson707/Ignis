#include "LoaderLight.h"
#include "Loader.h"
#include "Logger.h"
#include "serialization/VectorSerializer.h"
#include "skysun/SkyModel.h"
#include "skysun/SunLocation.h"

#include <chrono>

namespace IG {

enum LightType {
	LIGHT_POINT				   = 0x0,
	LIGHT_AREA				   = 0x1,
	LIGHT_DIRECTIONAL		   = 0x2,
	LIGHT_SUN				   = 0x3,
	LIGHT_SKY				   = 0x4,
	LIGHT_CIE_UNIFORM		   = 0x10,
	LIGHT_CIE_CLOUDY		   = 0x11,
	LIGHT_PEREZ				   = 0x12,
	LIGHT_ENVIRONMENT		   = 0x20,
	LIGHT_ENVIRONMENT_TEXTURED = 0x21
};

static ElevationAzimuth extractEA(const std::shared_ptr<Parser::Object>& obj)
{
	if (obj->property("direction").isValid()) {
		return ElevationAzimuth::fromDirection(obj->property("direction").getVector3(Vector3f(0, 0, 1)).normalized());
	} else if (obj->property("sun_direction").isValid()) {
		return ElevationAzimuth::fromDirection(obj->property("sun_direction").getVector3(Vector3f(0, 0, 1)).normalized());
	} else if (obj->property("theta").isValid() || obj->property("phi").isValid()) {
		return ElevationAzimuth::fromThetaPhi(obj->property("theta").getNumber(0), obj->property("phi").getNumber(0));
	} else if (obj->property("elevation").isValid() || obj->property("azimuth").isValid()) {
		return ElevationAzimuth{ obj->property("elevation").getNumber(0), obj->property("azimuth").getNumber(0) };
	} else {
		TimePoint timepoint;
		MapLocation location;
		timepoint.Year	   = obj->property("year").getInteger(timepoint.Year);
		timepoint.Month	   = obj->property("month").getInteger(timepoint.Month);
		timepoint.Day	   = obj->property("day").getInteger(timepoint.Day);
		timepoint.Hour	   = obj->property("hour").getInteger(timepoint.Hour);
		timepoint.Minute   = obj->property("minute").getInteger(timepoint.Minute);
		timepoint.Seconds  = obj->property("seconds").getNumber(timepoint.Seconds);
		location.Latitude  = obj->property("latitude").getNumber(location.Latitude);
		location.Longitude = obj->property("longitude").getNumber(location.Longitude);
		location.Timezone  = obj->property("timezone").getNumber(location.Timezone);
		return computeSunEA(timepoint, location);
	}
}

static uint32 setup_sky(const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& res)
{
	IG_UNUSED(ctx);

	auto ground	   = light->property("ground").getVector3(Vector3f(0.8f, 0.8f, 0.8f));
	auto turbidity = light->property("turbidity").getNumber(3.0f);
	auto ea		   = extractEA(light);

	SkyModel model(ground, ea, turbidity);

	uint32 id  = res.Database.BufferTable.entryCount();
	auto& data = res.Database.BufferTable.addLookup(0, 0, DefaultAlignment);

	VectorSerializer serializer(data, false);
	model.save(serializer);

	return id;
}

static void light_point(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	auto pos	   = light->property("position").getVector3();
	auto intensity = ctx.extractColor(light, "intensity");

	auto& data = result.Database.LightTable.addLookup(LIGHT_POINT, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(pos);
	serializer.write((uint32)0); // PADDING
	serializer.write(intensity);
}

static void light_area(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	const std::string entity = light->property("entity").getString();

	uint32 entity_id = 0;
	if (!ctx.Environment.EntityIDs.count(entity))
		IG_LOG(L_ERROR) << "No entity named '" << entity << "' exists for area light" << std::endl;
	else
		entity_id = ctx.Environment.EntityIDs.at(entity);

	auto radiance = ctx.extractColor(light, "radiance");
	auto& data	  = result.Database.LightTable.addLookup(LIGHT_AREA, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write((uint32)entity_id);
	serializer.write(radiance);
}

static void light_directional(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	auto ea			= extractEA(light);
	Vector3f dir	= ea.toDirection();
	auto irradiance = ctx.extractColor(light, "irradiance");

	auto& data = result.Database.LightTable.addLookup(LIGHT_DIRECTIONAL, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(dir);
	serializer.write((uint32)0); // PADDING
	serializer.write(irradiance);
}

static void light_sun(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);
	IG_UNUSED(ctx);

	auto ea		 = extractEA(light);
	Vector3f dir = ea.toDirection();

	auto power		= light->property("sun_scale").getNumber(1.0f);
	auto sun_radius = light->property("sun_radius_scale").getNumber(1.0f);

	auto& data = result.Database.LightTable.addLookup(LIGHT_SUN, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(dir);
	serializer.write(power);
	serializer.write(sun_radius);
}

static void light_sky(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	uint32 id = setup_sky(light, ctx, result);

	auto& data = result.Database.LightTable.addLookup(LIGHT_SKY, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(id);
}

static void light_sunsky(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	light_sky(name + "_sky", light, ctx, result);
	light_sun(name + "_sun", light, ctx, result);
}

static void light_cie_uniform(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	auto zenith			  = ctx.extractColor(light, "zenith");
	auto ground			  = ctx.extractColor(light, "ground");
	auto groundbrightness = light->property("ground_brightness").getNumber(0.2f);

	auto& data = result.Database.LightTable.addLookup(LIGHT_CIE_UNIFORM, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(zenith);
	serializer.write(groundbrightness);
	serializer.write(ground);
}

static void light_cie_cloudy(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	auto zenith			  = ctx.extractColor(light, "zenith");
	auto ground			  = ctx.extractColor(light, "ground");
	auto groundbrightness = light->property("ground_brightness").getNumber(0.2f);

	auto& data = result.Database.LightTable.addLookup(LIGHT_CIE_CLOUDY, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(zenith);
	serializer.write(groundbrightness);
	serializer.write(ground);
}

static inline float perez_model(float zenithAngle, float sunAngle, float a, float b, float c, float d, float e)
{
	float zc = std::cos(zenithAngle);
	float sc = std::cos(sunAngle);

	float A = 1 + a * std::exp(b * zc);
	float B = 1 + c * std::exp(d * sunAngle) + e * sc * sc;
	return A * B;
}

static void light_perez(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);

	auto ea		 = extractEA(light);
	Vector3f dir = ea.toDirection();

	auto a = light->property("a").getNumber(1.0f);
	auto b = light->property("b").getNumber(1.0f);
	auto c = light->property("c").getNumber(1.0f);
	auto d = light->property("d").getNumber(1.0f);
	auto e = light->property("e").getNumber(1.0f);

	Vector3f color;
	if (light->properties().count("luminance")) {
		color = ctx.extractColor(light, "luminance");
	} else {
		auto zenith			= ctx.extractColor(light, "zenith");
		const float groundZ = perez_model(0, -dir(2), a, b, c, d, e); // TODO: Validate
		color				= zenith * groundZ;
	}

	auto& data = result.Database.LightTable.addLookup(LIGHT_PEREZ, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);
	serializer.write(dir);
	serializer.write(a);
	serializer.write(b);
	serializer.write(c);
	serializer.write(d);
	serializer.write(e);
	serializer.write(color);
}

static void light_env(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result)
{
	IG_UNUSED(name);
	
	bool isTexture = ctx.isTexture(light, "radiance");

	const LightType type = isTexture ? LIGHT_ENVIRONMENT_TEXTURED : LIGHT_ENVIRONMENT;
	auto& data			 = result.Database.LightTable.addLookup(type, 0, DefaultAlignment);
	VectorSerializer serializer(data, false);

	if (isTexture) {
		const uint32 id = ctx.extractTextureID(light, "radiance");
		serializer.write(id);
	} else {
		const Vector3f color = ctx.extractColor(light, "radiance");
		serializer.write(color);
	}
}

using LightLoader = void (*)(const std::string& name, const std::shared_ptr<Parser::Object>& light, const LoaderContext& ctx, LoaderResult& result);
static struct {
	const char* Name;
	LightLoader Loader;
} _generators[] = {
	{ "point", light_point },
	{ "area", light_area },
	{ "directional", light_directional },
	{ "direction", light_directional },
	{ "sun", light_sun },
	{ "sunsky", light_sunsky },
	{ "skysun", light_sunsky },
	{ "sky", light_sky },
	{ "cie_uniform", light_cie_uniform },
	{ "cieuniform", light_cie_uniform },
	{ "cie_cloudy", light_cie_cloudy },
	{ "ciecloudy", light_cie_cloudy },
	{ "perez", light_perez },
	{ "uniform", light_env },
	{ "env", light_env },
	{ "envmap", light_env },
	{ "constant", light_env },
	{ "", nullptr }
};

bool LoaderLight::load(LoaderContext& ctx, LoaderResult& result)
{
	const auto start1 = std::chrono::high_resolution_clock::now();
	for (const auto& pair : ctx.Scene.lights()) {
		const auto light = pair.second;

		bool found = false;
		for (size_t i = 0; _generators[i].Loader; ++i) {
			if (_generators[i].Name == light->pluginType()) {
				_generators[i].Loader(pair.first, light, ctx, result);
				found = true;
				break;
			}
		}
		if (!found)
			IG_LOG(L_ERROR) << "No light type '" << light->pluginType() << "' available" << std::endl;
	}

	IG_LOG(L_DEBUG) << "Storing lights took " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start1).count() / 1000.0f << " seconds" << std::endl;

	return true;
}

bool LoaderLight::setup_area(LoaderContext& ctx)
{
	const auto start1 = std::chrono::high_resolution_clock::now();

	uint32 counter = 0;
	for (const auto& pair : ctx.Scene.lights()) {
		const auto light = pair.second;

		if (light->pluginType() == "area") {
			const std::string entity		= light->property("entity").getString();
			ctx.Environment.AreaIDs[entity] = counter;
		}

		++counter;
	}

	if (ctx.Environment.AreaIDs.empty()) {
		IG_LOG(L_DEBUG) << "Storing Area lights took " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start1).count() / 1000.0f << " seconds" << std::endl;
	}

	return true;
}

} // namespace IG