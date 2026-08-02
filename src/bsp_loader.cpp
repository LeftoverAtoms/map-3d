#include "bsp_format.h"

#include <array>
#include <cmath>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace q3::bsp
{
	namespace
	{
		using FilePtr = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

		struct trGlobals_t
		{
			struct shaderRemap_t
			{
				std::string oldShader;
				std::string newShader;
				std::string timeOffset;
			};

			bool worldMapLoaded = false;
			bool vertexLight = false;
			const std::byte *externalVisData = nullptr;
			world_t *world = nullptr;
			vec3_t sunDirection {};
			std::vector<shaderRemap_t> shaderRemaps;
		};

		static world_t s_worldData;
		static trGlobals_t tr;
		static const std::uint8_t *fileBase;
		static std::int32_t c_gridVerts;

		inline std::int32_t LittleLong(std::int32_t p_value)
		{
			return p_value;
		}

		inline float LittleFloat(float p_value)
		{
			return p_value;
		}

		inline std::uint8_t PlaneTypeForNormal(const float (&normal)[3]) noexcept
		{
			if (normal[0] == 1.0f) {
				return 0;
			}
			if (normal[1] == 1.0f) {
				return 1;
			}
			if (normal[2] == 1.0f) {
				return 2;
			}
			return 3;
		}

		inline float DotProduct(const vec3_t a, const vec3_t b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		inline void VectorAdd(const vec3_t a, const vec3_t b, vec3_t out)
		{
			out[0] = a[0] + b[0];
			out[1] = a[1] + b[1];
			out[2] = a[2] + b[2];
		}

		inline void VectorScale(const vec3_t in, float scale, vec3_t out)
		{
			out[0] = in[0] * scale;
			out[1] = in[1] * scale;
			out[2] = in[2] * scale;
		}

		inline void VectorSubtract(const vec3_t a, const vec3_t b, vec3_t out)
		{
			out[0] = a[0] - b[0];
			out[1] = a[1] - b[1];
			out[2] = a[2] - b[2];
		}

		inline float VectorLength(const vec3_t in)
		{
			return std::sqrt(DotProduct(in, in));
		}

		inline float VectorLengthSquared(const vec3_t in)
		{
			return DotProduct(in, in);
		}

		inline void VectorClear(vec3_t out)
		{
			out[0] = out[1] = out[2] = 0.0f;
		}

		inline void VectorCopy(const vec3_t in, vec3_t out)
		{
			out[0] = in[0];
			out[1] = in[1];
			out[2] = in[2];
		}

		inline float VectorNormalize2(const vec3_t in, vec3_t out)
		{
			const float length = VectorLength(in);
			if (length == 0.0f) {
				VectorClear(out);
				return 0.0f;
			}
			const float ilength = 1.0f / length;
			VectorScale(in, ilength, out);
			return length;
		}

		inline void VectorNormalize(vec3_t inout)
		{
			VectorNormalize2(inout, inout);
		}

		inline void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross)
		{
			cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
			cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
			cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
		}

		inline void ClearBounds(vec3_t mins, vec3_t maxs)
		{
			mins[0] = mins[1] = mins[2] = 99999.0f;
			maxs[0] = maxs[1] = maxs[2] = -99999.0f;
		}

		inline void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs)
		{
			for (std::int32_t i = 0; i < 3; ++i) {
				if (v[i] < mins[i]) {
					mins[i] = v[i];
				}
				if (v[i] > maxs[i]) {
					maxs[i] = v[i];
				}
			}
		}

		inline void SetPlaneSignbits(cplane_t *out)
		{
			std::uint8_t bits = 0;
			for (std::int32_t j = 0; j < 3; ++j) {
				if (out->normal[j] < 0.0f) {
					bits |= 1 << j;
				}
			}
			out->signbits = bits;
		}

		std::string COM_ParseExt(char **data_p, bool allowLineBreaks)
		{
			char *data = *data_p;
			std::string token;

			while (*data && static_cast<unsigned char>(*data) <= ' ') {
				if (!allowLineBreaks && (*data == '\n' || *data == '\r')) {
					*data_p = data;
					return token;
				}
				++data;
			}

			if (!*data) {
				*data_p = data;
				return token;
			}

			if (*data == '"') {
				++data;
				while (*data && *data != '"') {
					token.push_back(*data++);
				}
				if (*data == '"') {
					++data;
				}
				*data_p = data;
				return token;
			}

			while (*data && static_cast<unsigned char>(*data) > ' ') {
				token.push_back(*data++);
			}
			*data_p = data;
			return token;
		}

		void R_ColorShiftLightingBytes(const std::uint8_t in[4], std::uint8_t out[4])
		{
			constexpr std::int32_t r_mapOverBrightBits = 2;
			constexpr std::int32_t tr_overbrightBits = 1;
			const std::int32_t shift = r_mapOverBrightBits - tr_overbrightBits;

			std::int32_t r = in[0] << shift;
			std::int32_t g = in[1] << shift;
			std::int32_t b = in[2] << shift;

			if ((r | g | b) > 255) {
				std::int32_t max = r > g ? r : g;
				max = max > b ? max : b;
				r = r * 255 / max;
				g = g * 255 / max;
				b = b * 255 / max;
			}

			out[0] = static_cast<std::uint8_t>(r);
			out[1] = static_cast<std::uint8_t>(g);
			out[2] = static_cast<std::uint8_t>(b);
			out[3] = in[3];
		}

		shader_t *R_FindShader(const char *name, std::int32_t lightmapNum, bool mipRawImage)
		{
			for (const std::unique_ptr<shader_t> &shader : s_worldData.rendererShaders) {
				if (std::strcmp(shader->name, name) == 0 && shader->lightmapNum == lightmapNum) {
					return shader.get();
				}
			}

			auto shader = std::make_unique<shader_t>();
			std::snprintf(shader->name, sizeof(shader->name), "%s", name);
			shader->shaderNum = static_cast<std::int32_t>(s_worldData.rendererShaders.size());
			shader->lightmapNum = lightmapNum;
			shader->remappedShader = nullptr;
			shader->timeOffset = 0.0f;
			shader->defaultShader = false;
			shader->isSky = false;
			shader->fogParms.color[0] = 1.0f;
			shader->fogParms.color[1] = 1.0f;
			shader->fogParms.color[2] = 1.0f;
			shader->fogParms.depthForOpaque = 1.0f;
			s_worldData.rendererShaders.push_back(std::move(shader));
			(void)mipRawImage;
			shader_t *created = s_worldData.rendererShaders.back().get();
			for (const trGlobals_t::shaderRemap_t &remap : tr.shaderRemaps) {
				if (remap.oldShader == created->name) {
					created->remappedShader = R_FindShader(remap.newShader.c_str(), 0, true);
					created->remappedShader->timeOffset = std::strtof(remap.timeOffset.c_str(), nullptr);
					break;
				}
			}
			return created;
		}

		shader_t *R_FindShaderByName(const char *name)
		{
			for (const std::unique_ptr<shader_t> &shader : s_worldData.rendererShaders) {
				if (std::strcmp(shader->name, name) == 0) {
					return shader.get();
				}
			}
			return nullptr;
		}

		std::uint32_t ColorBytes4(float r, float g, float b, float a)
		{
			std::uint32_t i = 0;
			auto *bytes = reinterpret_cast<std::uint8_t *>(&i);
			bytes[0] = static_cast<std::uint8_t>(r * 255.0f);
			bytes[1] = static_cast<std::uint8_t>(g * 255.0f);
			bytes[2] = static_cast<std::uint8_t>(b * 255.0f);
			bytes[3] = static_cast<std::uint8_t>(a * 255.0f);
			return i;
		}

		shader_t *ShaderForShaderNum(std::int32_t shaderNum, std::int32_t lightmapNum)
		{
			shader_t *shader;
			dshader_t *dsh;

			shaderNum = LittleLong(shaderNum);
			ERR_FAIL_COND_V_MSG(shaderNum < 0 || shaderNum >= s_worldData.numShaders, nullptr,
				vformat("ShaderForShaderNum: bad num %d", shaderNum));

			dsh = &s_worldData.shaders[shaderNum];
			shader = R_FindShader(dsh->shader, lightmapNum, true);
			if (shader->defaultShader && !s_worldData.rendererShaders.empty()) {
				return s_worldData.rendererShaders.front().get();
			}

			return shader;
		}

		void R_RemapShader(const char *oldShader, const char *newShader, const char *timeOffset)
		{
			shader_t *sh;
			shader_t *sh2;

			tr.shaderRemaps.push_back({
				oldShader ? oldShader : "",
				newShader ? newShader : "",
				timeOffset ? timeOffset : "",
			});

			sh = R_FindShaderByName(oldShader);
			if (!sh) {
				sh = R_FindShader(oldShader, 0, true);
			}

			sh2 = R_FindShaderByName(newShader);
			if (!sh2) {
				sh2 = R_FindShader(newShader, 0, true);
			}

			if (sh && sh2) {
				if (sh != sh2) {
					sh->remappedShader = sh2;
				} else {
					sh->remappedShader = nullptr;
				}
				if (timeOffset) {
					sh2->timeOffset = std::strtof(timeOffset, nullptr);
				}
			}
		}

		void LerpDrawVert(drawVert_t *a, drawVert_t *b, drawVert_t *out)
		{
			out->xyz[0] = 0.5f * (a->xyz[0] + b->xyz[0]);
			out->xyz[1] = 0.5f * (a->xyz[1] + b->xyz[1]);
			out->xyz[2] = 0.5f * (a->xyz[2] + b->xyz[2]);
			out->st[0] = 0.5f * (a->st[0] + b->st[0]);
			out->st[1] = 0.5f * (a->st[1] + b->st[1]);
			out->lightmap[0] = 0.5f * (a->lightmap[0] + b->lightmap[0]);
			out->lightmap[1] = 0.5f * (a->lightmap[1] + b->lightmap[1]);
			out->color[0] = static_cast<std::uint8_t>((a->color[0] + b->color[0]) >> 1);
			out->color[1] = static_cast<std::uint8_t>((a->color[1] + b->color[1]) >> 1);
			out->color[2] = static_cast<std::uint8_t>((a->color[2] + b->color[2]) >> 1);
			out->color[3] = static_cast<std::uint8_t>((a->color[3] + b->color[3]) >> 1);
		}

		using gridCtrl_t = std::array<std::array<drawVert_t, MAX_GRID_SIZE>, MAX_GRID_SIZE>;
		using errorTable_t = std::array<std::array<float, MAX_GRID_SIZE>, 2>;

		void Transpose(std::int32_t width, std::int32_t height, gridCtrl_t &ctrl)
		{
			drawVert_t temp;

			if (width > height) {
				for (std::int32_t i = 0; i < height; ++i) {
					for (std::int32_t j = i + 1; j < width; ++j) {
						if (j < height) {
							temp = ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
							ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
							ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = temp;
						} else {
							ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
						}
					}
				}
			} else {
				for (std::int32_t i = 0; i < width; ++i) {
					for (std::int32_t j = i + 1; j < height; ++j) {
						if (j < width) {
							temp = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
							ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
							ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = temp;
						} else {
							ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
						}
					}
				}
			}
		}

		void InvertCtrl(std::int32_t width, std::int32_t height, gridCtrl_t &ctrl)
		{
			for (std::int32_t i = 0; i < height; ++i) {
				for (std::int32_t j = 0; j < width / 2; ++j) {
					const drawVert_t temp = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
					ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(width - 1 - j)];
					ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(width - 1 - j)] = temp;
				}
			}
		}

		void InvertErrorTable(errorTable_t &errorTable, std::int32_t width, std::int32_t height)
		{
			errorTable_t copy = errorTable;

			for (std::int32_t i = 0; i < width; ++i) {
				errorTable[1][static_cast<std::size_t>(i)] = copy[0][static_cast<std::size_t>(i)];
			}
			for (std::int32_t i = 0; i < height; ++i) {
				errorTable[0][static_cast<std::size_t>(i)] = copy[1][static_cast<std::size_t>(height - 1 - i)];
			}
		}

		void PutPointsOnCurve(gridCtrl_t &ctrl, std::int32_t width, std::int32_t height)
		{
			drawVert_t prev;
			drawVert_t next;

			for (std::int32_t i = 0; i < width; ++i) {
				for (std::int32_t j = 1; j < height; j += 2) {
					LerpDrawVert(&ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], &ctrl[static_cast<std::size_t>(j + 1)][static_cast<std::size_t>(i)], &prev);
					LerpDrawVert(&ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], &ctrl[static_cast<std::size_t>(j - 1)][static_cast<std::size_t>(i)], &next);
					LerpDrawVert(&prev, &next, &ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
				}
			}

			for (std::int32_t j = 0; j < height; ++j) {
				for (std::int32_t i = 1; i < width; i += 2) {
					LerpDrawVert(&ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], &ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i + 1)], &prev);
					LerpDrawVert(&ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], &ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i - 1)], &next);
					LerpDrawVert(&prev, &next, &ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
				}
			}
		}

		void MakeMeshNormals(std::int32_t width, std::int32_t height, gridCtrl_t &ctrl)
		{
			static constexpr std::int32_t neighbors[8][2] = {
				{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}
			};

			bool wrapWidth = true;
			for (std::int32_t i = 0; i < height; ++i) {
				vec3_t delta;
				VectorSubtract(ctrl[static_cast<std::size_t>(i)][0].xyz, ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(width - 1)].xyz, delta);
				if (VectorLengthSquared(delta) > 1.0f) {
					wrapWidth = false;
					break;
				}
			}

			bool wrapHeight = true;
			for (std::int32_t i = 0; i < width; ++i) {
				vec3_t delta;
				VectorSubtract(ctrl[0][static_cast<std::size_t>(i)].xyz, ctrl[static_cast<std::size_t>(height - 1)][static_cast<std::size_t>(i)].xyz, delta);
				if (VectorLengthSquared(delta) > 1.0f) {
					wrapHeight = false;
					break;
				}
			}

			for (std::int32_t i = 0; i < width; ++i) {
				for (std::int32_t j = 0; j < height; ++j) {
					drawVert_t *dv = &ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
					vec3_t base;
					VectorCopy(dv->xyz, base);
					std::array<vec3_t, 8> around {};
					std::array<bool, 8> good {};

					for (std::int32_t k = 0; k < 8; ++k) {
						for (std::int32_t dist = 1; dist <= 3; ++dist) {
							std::int32_t x = i + neighbors[k][0] * dist;
							std::int32_t y = j + neighbors[k][1] * dist;
							if (wrapWidth) {
								if (x < 0) {
									x = width - 1 + x;
								} else if (x >= width) {
									x = 1 + x - width;
								}
							}
							if (wrapHeight) {
								if (y < 0) {
									y = height - 1 + y;
								} else if (y >= height) {
									y = 1 + y - height;
								}
							}
							if (x < 0 || x >= width || y < 0 || y >= height) {
								break;
							}
							vec3_t temp;
							VectorSubtract(ctrl[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].xyz, base, temp);
							if (VectorNormalize2(temp, temp) == 0.0f) {
								continue;
							}
							good[static_cast<std::size_t>(k)] = true;
							VectorCopy(temp, around[static_cast<std::size_t>(k)]);
							break;
						}
					}

					vec3_t sum;
					VectorClear(sum);
					for (std::int32_t k = 0; k < 8; ++k) {
						if (!good[static_cast<std::size_t>(k)] || !good[static_cast<std::size_t>((k + 1) & 7)]) {
							continue;
						}
						vec3_t normal;
						CrossProduct(around[static_cast<std::size_t>((k + 1) & 7)], around[static_cast<std::size_t>(k)], normal);
						if (VectorNormalize2(normal, normal) == 0.0f) {
							continue;
						}
						VectorAdd(normal, sum, sum);
					}
					VectorNormalize2(sum, dv->normal);
				}
			}
		}

		std::shared_ptr<srfGridMesh_t> R_CreateSurfaceGridMesh(std::int32_t width, std::int32_t height, gridCtrl_t &ctrl, errorTable_t &errorTable)
		{
			auto grid = std::make_shared<srfGridMesh_t>();
			grid->surfaceType = SF_GRID;
			grid->dlightBits[0] = 0;
			grid->dlightBits[1] = 0;
			grid->width = width;
			grid->height = height;
			grid->widthLodError.assign(errorTable[0].begin(), errorTable[0].begin() + width);
			grid->heightLodError.assign(errorTable[1].begin(), errorTable[1].begin() + height);
			grid->verts.resize(static_cast<std::size_t>(width * height));
			c_gridVerts += width * height;
			ClearBounds(grid->meshBounds[0], grid->meshBounds[1]);

			for (std::int32_t i = 0; i < width; ++i) {
				for (std::int32_t j = 0; j < height; ++j) {
					drawVert_t *vert = &grid->verts[static_cast<std::size_t>(j * width + i)];
					*vert = ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
					AddPointToBounds(vert->xyz, grid->meshBounds[0], grid->meshBounds[1]);
				}
			}

			VectorAdd(grid->meshBounds[0], grid->meshBounds[1], grid->localOrigin);
			VectorScale(grid->localOrigin, 0.5f, grid->localOrigin);
			vec3_t tmpVec;
			VectorSubtract(grid->meshBounds[0], grid->localOrigin, tmpVec);
			grid->meshRadius = VectorLength(tmpVec);
			VectorCopy(grid->localOrigin, grid->lodOrigin);
			grid->lodRadius = grid->meshRadius;
			return grid;
		}

		std::shared_ptr<srfGridMesh_t> R_SubdividePatchToGrid(std::int32_t width, std::int32_t height, drawVert_t points[MAX_PATCH_SIZE * MAX_PATCH_SIZE])
		{
			drawVert_t prev;
			drawVert_t next;
			drawVert_t mid;
			gridCtrl_t ctrl {};
			errorTable_t errorTable {};

			for (std::int32_t i = 0; i < width; ++i) {
				for (std::int32_t j = 0; j < height; ++j) {
					ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = points[j * width + i];
				}
			}

			for (std::int32_t dir = 0; dir < 2; ++dir) {
				for (std::int32_t j = 0; j < MAX_GRID_SIZE; ++j) {
					errorTable[static_cast<std::size_t>(dir)][static_cast<std::size_t>(j)] = 0.0f;
				}

				for (std::int32_t j = 0; j + 2 < width; j += 2) {
					float maxLen = 0.0f;
					for (std::int32_t i = 0; i < height; ++i) {
						vec3_t midxyz;
						vec3_t midxyz2;
						vec3_t dirVec;
						vec3_t projected;

						for (std::int32_t l = 0; l < 3; ++l) {
							midxyz[l] = (ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)].xyz[l] +
									ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)].xyz[l] * 2.0f +
									ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 2)].xyz[l]) * 0.25f;
						}

						VectorSubtract(midxyz, ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)].xyz, midxyz);
						VectorSubtract(ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 2)].xyz, ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)].xyz, dirVec);
						VectorNormalize(dirVec);

						const float d = DotProduct(midxyz, dirVec);
						VectorScale(dirVec, d, projected);
						VectorSubtract(midxyz, projected, midxyz2);
						const float len = VectorLengthSquared(midxyz2);
						if (len > maxLen) {
							maxLen = len;
						}
					}

					maxLen = std::sqrt(maxLen);
					if (maxLen < 0.1f) {
						errorTable[static_cast<std::size_t>(dir)][static_cast<std::size_t>(j + 1)] = 999.0f;
						continue;
					}
					if (width + 2 > MAX_GRID_SIZE) {
						errorTable[static_cast<std::size_t>(dir)][static_cast<std::size_t>(j + 1)] = 1.0f / maxLen;
						continue;
					}
					if (maxLen <= 8.0f) {
						errorTable[static_cast<std::size_t>(dir)][static_cast<std::size_t>(j + 1)] = 1.0f / maxLen;
						continue;
					}

					errorTable[static_cast<std::size_t>(dir)][static_cast<std::size_t>(j + 2)] = 1.0f / maxLen;
					width += 2;
					for (std::int32_t i = 0; i < height; ++i) {
						LerpDrawVert(&ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)], &ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)], &prev);
						LerpDrawVert(&ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)], &ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 2)], &next);
						LerpDrawVert(&prev, &next, &mid);

						for (std::int32_t k = width - 1; k > j + 3; --k) {
							ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(k - 2)];
						}
						ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)] = prev;
						ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 2)] = mid;
						ctrl[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 3)] = next;
					}
					j -= 2;
				}

				Transpose(width, height, ctrl);
				const std::int32_t t = width;
				width = height;
				height = t;
			}

			PutPointsOnCurve(ctrl, width, height);

			for (std::int32_t i = 1; i < width - 1; ++i) {
				if (errorTable[0][static_cast<std::size_t>(i)] != 999.0f) {
					continue;
				}
				for (std::int32_t j = i + 1; j < width; ++j) {
					for (std::int32_t k = 0; k < height; ++k) {
						ctrl[static_cast<std::size_t>(k)][static_cast<std::size_t>(j - 1)] = ctrl[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)];
					}
					errorTable[0][static_cast<std::size_t>(j - 1)] = errorTable[0][static_cast<std::size_t>(j)];
				}
				--width;
			}

			for (std::int32_t i = 1; i < height - 1; ++i) {
				if (errorTable[1][static_cast<std::size_t>(i)] != 999.0f) {
					continue;
				}
				for (std::int32_t j = i + 1; j < height; ++j) {
					for (std::int32_t k = 0; k < width; ++k) {
						ctrl[static_cast<std::size_t>(j - 1)][static_cast<std::size_t>(k)] = ctrl[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
					}
					errorTable[1][static_cast<std::size_t>(j - 1)] = errorTable[1][static_cast<std::size_t>(j)];
				}
				--height;
			}

			if (height > width) {
				Transpose(width, height, ctrl);
				InvertErrorTable(errorTable, width, height);
				const std::int32_t t = width;
				width = height;
				height = t;
				InvertCtrl(width, height, ctrl);
			}

			MakeMeshNormals(width, height, ctrl);

			return R_CreateSurfaceGridMesh(width, height, ctrl, errorTable);
		}

		void ParseFace(const dsurface_t *ds, drawVert_t *verts, msurface_t *surf, std::int32_t *indexes)
		{
			std::int32_t i, j;
			std::int32_t numPoints;
			std::int32_t numIndexes;
			std::int32_t lightmapNum;

			surf->fogIndex = LittleLong(ds->fogNum) + 1;
			lightmapNum = LittleLong(ds->lightmapNum);
			surf->shader = ShaderForShaderNum(ds->shaderNum, lightmapNum);

			numPoints = LittleLong(ds->numVerts);
			if (numPoints > MAX_FACE_POINTS) {
				numPoints = MAX_FACE_POINTS;
			}

			numIndexes = LittleLong(ds->numIndexes);

			auto cv = std::make_shared<srfSurfaceFace_t>();
			cv->surfaceType = SF_FACE;
			cv->dlightBits[0] = 0;
			cv->dlightBits[1] = 0;
			cv->numPoints = numPoints;
			cv->numIndices = numIndexes;
			cv->points.resize(static_cast<std::size_t>(numPoints) * 8U);
			cv->indices.resize(static_cast<std::size_t>(numIndexes));

			verts += LittleLong(ds->firstVert);
			for (i = 0; i < numPoints; ++i) {
				for (j = 0; j < 3; ++j) {
					cv->points[static_cast<std::size_t>(i * 8 + j)] = LittleFloat(verts[i].xyz[j]);
				}
				for (j = 0; j < 2; ++j) {
					cv->points[static_cast<std::size_t>(i * 8 + 3 + j)] = LittleFloat(verts[i].st[j]);
					cv->points[static_cast<std::size_t>(i * 8 + 5 + j)] = LittleFloat(verts[i].lightmap[j]);
				}
				std::uint8_t color[4];
				R_ColorShiftLightingBytes(verts[i].color, color);
				std::memcpy(&cv->points[static_cast<std::size_t>(i * 8 + 7)], color, 4);
			}

			indexes += LittleLong(ds->firstIndex);
			for (i = 0; i < numIndexes; ++i) {
				cv->indices[static_cast<std::size_t>(i)] = LittleLong(indexes[i]);
			}

			vec3_t firstPoint;
			for (i = 0; i < 3; ++i) {
				firstPoint[i] = cv->points[static_cast<std::size_t>(i)];
				cv->plane.normal[i] = LittleFloat(ds->lightmapVecs[2][i]);
			}
			cv->plane.dist = DotProduct(firstPoint, cv->plane.normal);
			SetPlaneSignbits(&cv->plane);
			cv->plane.type = PlaneTypeForNormal(cv->plane.normal);
			(void)lightmapNum;

			surf->data = cv;
		}

		void ParseMesh(const dsurface_t *ds, drawVert_t *verts, msurface_t *surf)
		{
			std::int32_t i, j;
			std::int32_t width;
			std::int32_t height;
			std::int32_t numPoints;
			vec3_t bounds[2];
			vec3_t tmpVec;

			surf->fogIndex = LittleLong(ds->fogNum) + 1;
			surf->shader = ShaderForShaderNum(ds->shaderNum, LittleLong(ds->lightmapNum));

			if (s_worldData.shaders[LittleLong(ds->shaderNum)].surfaceFlags & SURF_NODRAW) {
				auto skipData = std::make_shared<surfaceData_t>();
				skipData->surfaceType = SF_SKIP;
				surf->data = skipData;
				return;
			}

			width = LittleLong(ds->patchWidth);
			height = LittleLong(ds->patchHeight);

			verts += LittleLong(ds->firstVert);
			numPoints = width * height;
			std::vector<drawVert_t> points(static_cast<std::size_t>(numPoints));
			for (i = 0; i < numPoints; ++i) {
				for (j = 0; j < 3; ++j) {
					points[static_cast<std::size_t>(i)].xyz[j] = LittleFloat(verts[i].xyz[j]);
					points[static_cast<std::size_t>(i)].normal[j] = LittleFloat(verts[i].normal[j]);
				}
				for (j = 0; j < 2; ++j) {
					points[static_cast<std::size_t>(i)].st[j] = LittleFloat(verts[i].st[j]);
					points[static_cast<std::size_t>(i)].lightmap[j] = LittleFloat(verts[i].lightmap[j]);
				}
				R_ColorShiftLightingBytes(verts[i].color, points[static_cast<std::size_t>(i)].color);
			}

			auto grid = R_SubdividePatchToGrid(width, height, points.data());
			surf->data = grid;

			for (i = 0; i < 3; ++i) {
				bounds[0][i] = LittleFloat(ds->lightmapVecs[0][i]);
				bounds[1][i] = LittleFloat(ds->lightmapVecs[1][i]);
			}
			VectorAdd(bounds[0], bounds[1], bounds[1]);
			VectorScale(bounds[1], 0.5f, grid->lodOrigin);
			VectorSubtract(bounds[0], grid->lodOrigin, tmpVec);
			grid->lodRadius = VectorLength(tmpVec);
		}

		void ParseTriSurf(const dsurface_t *ds, drawVert_t *verts, msurface_t *surf, std::int32_t *indexes)
		{
			std::int32_t i, j;
			std::int32_t numVerts;
			std::int32_t numIndexes;

			surf->fogIndex = LittleLong(ds->fogNum) + 1;
			surf->shader = ShaderForShaderNum(ds->shaderNum, LIGHTMAP_BY_VERTEX);

			numVerts = LittleLong(ds->numVerts);
			numIndexes = LittleLong(ds->numIndexes);

			auto tri = std::make_shared<srfTriangles_t>();
			tri->surfaceType = SF_TRIANGLES;
			tri->dlightBits[0] = 0;
			tri->dlightBits[1] = 0;
			tri->numVerts = numVerts;
			tri->numIndexes = numIndexes;
			tri->verts.resize(static_cast<std::size_t>(numVerts));
			tri->indexes.resize(static_cast<std::size_t>(numIndexes));
			surf->data = tri;

			ClearBounds(tri->bounds[0], tri->bounds[1]);
			verts += LittleLong(ds->firstVert);
			for (i = 0; i < numVerts; ++i) {
				for (j = 0; j < 3; ++j) {
					tri->verts[static_cast<std::size_t>(i)].xyz[j] = LittleFloat(verts[i].xyz[j]);
					tri->verts[static_cast<std::size_t>(i)].normal[j] = LittleFloat(verts[i].normal[j]);
				}
				AddPointToBounds(tri->verts[static_cast<std::size_t>(i)].xyz, tri->bounds[0], tri->bounds[1]);
				for (j = 0; j < 2; ++j) {
					tri->verts[static_cast<std::size_t>(i)].st[j] = LittleFloat(verts[i].st[j]);
					tri->verts[static_cast<std::size_t>(i)].lightmap[j] = LittleFloat(verts[i].lightmap[j]);
				}
				R_ColorShiftLightingBytes(verts[i].color, tri->verts[static_cast<std::size_t>(i)].color);
			}

			indexes += LittleLong(ds->firstIndex);
			for (i = 0; i < numIndexes; ++i) {
				tri->indexes[static_cast<std::size_t>(i)] = LittleLong(indexes[i]);
				if (tri->indexes[static_cast<std::size_t>(i)] < 0 || tri->indexes[static_cast<std::size_t>(i)] >= numVerts) {
					tri->indexes[static_cast<std::size_t>(i)] = 0;
				}
			}

			VectorAdd(tri->bounds[0], tri->bounds[1], tri->localOrigin);
			VectorScale(tri->localOrigin, 0.5f, tri->localOrigin);
			vec3_t tmpVec;
			VectorSubtract(tri->bounds[0], tri->localOrigin, tmpVec);
			tri->radius = VectorLength(tmpVec);
		}

		void ParseFlare(const dsurface_t *ds, drawVert_t *, msurface_t *surf, std::int32_t *)
		{
			auto flare = std::make_shared<srfFlare_t>();
			flare->surfaceType = SF_FLARE;
			surf->fogIndex = LittleLong(ds->fogNum) + 1;
			surf->shader = ShaderForShaderNum(ds->shaderNum, LIGHTMAP_BY_VERTEX);

			for (std::int32_t i = 0; i < 3; ++i) {
				flare->origin[i] = LittleFloat(ds->lightmapOrigin[i]);
				flare->normal[i] = LittleFloat(ds->lightmapVecs[2][i]);
				flare->color[i] = LittleFloat(ds->lightmapVecs[0][i]);
			}
			surf->data = flare;
		}

		bool R_MergedWidthPoints(srfGridMesh_t *grid, std::int32_t offset)
		{
			for (std::int32_t i = 1; i < grid->width - 1; ++i) {
				for (std::int32_t j = i + 1; j < grid->width - 1; ++j) {
					if (std::fabs(grid->verts[static_cast<std::size_t>(i + offset)].xyz[0] - grid->verts[static_cast<std::size_t>(j + offset)].xyz[0]) > 0.1f) {
						continue;
					}
					if (std::fabs(grid->verts[static_cast<std::size_t>(i + offset)].xyz[1] - grid->verts[static_cast<std::size_t>(j + offset)].xyz[1]) > 0.1f) {
						continue;
					}
					if (std::fabs(grid->verts[static_cast<std::size_t>(i + offset)].xyz[2] - grid->verts[static_cast<std::size_t>(j + offset)].xyz[2]) > 0.1f) {
						continue;
					}
					return true;
				}
			}
			return false;
		}

		bool R_MergedHeightPoints(srfGridMesh_t *grid, std::int32_t offset)
		{
			for (std::int32_t i = 1; i < grid->height - 1; ++i) {
				for (std::int32_t j = i + 1; j < grid->height - 1; ++j) {
					if (std::fabs(grid->verts[static_cast<std::size_t>(grid->width * i + offset)].xyz[0] - grid->verts[static_cast<std::size_t>(grid->width * j + offset)].xyz[0]) > 0.1f) {
						continue;
					}
					if (std::fabs(grid->verts[static_cast<std::size_t>(grid->width * i + offset)].xyz[1] - grid->verts[static_cast<std::size_t>(grid->width * j + offset)].xyz[1]) > 0.1f) {
						continue;
					}
					if (std::fabs(grid->verts[static_cast<std::size_t>(grid->width * i + offset)].xyz[2] - grid->verts[static_cast<std::size_t>(grid->width * j + offset)].xyz[2]) > 0.1f) {
						continue;
					}
					return true;
				}
			}
			return false;
		}

		bool R_EqualVert(const drawVert_t &a, const drawVert_t &b)
		{
			if (std::fabs(a.xyz[0] - b.xyz[0]) > 0.1f) {
				return false;
			}
			if (std::fabs(a.xyz[1] - b.xyz[1]) > 0.1f) {
				return false;
			}
			if (std::fabs(a.xyz[2] - b.xyz[2]) > 0.1f) {
				return false;
			}
			return true;
		}

		void R_FixSharedVertexLodError_r(std::int32_t start, srfGridMesh_t *grid1)
		{
			for (std::int32_t j = start; j < s_worldData.numsurfaces; ++j) {
				auto grid2Ref = std::dynamic_pointer_cast<srfGridMesh_t>(s_worldData.surfaces[j].data);
				if (!grid2Ref) {
					continue;
				}
				srfGridMesh_t *grid2 = grid2Ref.get();
				if (grid2->surfaceType != SF_GRID) {
					continue;
				}
				if (grid2->lodFixed == 2) {
					continue;
				}
				if (grid1->lodRadius != grid2->lodRadius) {
					continue;
				}
				if (grid1->lodOrigin[0] != grid2->lodOrigin[0]) {
					continue;
				}
				if (grid1->lodOrigin[1] != grid2->lodOrigin[1]) {
					continue;
				}
				if (grid1->lodOrigin[2] != grid2->lodOrigin[2]) {
					continue;
				}

				bool touch = false;
				for (std::int32_t n = 0; n < 2; ++n) {
					const std::int32_t offset1 = n ? (grid1->height - 1) * grid1->width : 0;
					if (R_MergedWidthPoints(grid1, offset1)) {
						continue;
					}
					for (std::int32_t k = 1; k < grid1->width - 1; ++k) {
						for (std::int32_t m = 0; m < 2; ++m) {
							const std::int32_t offset2 = m ? (grid2->height - 1) * grid2->width : 0;
							if (R_MergedWidthPoints(grid2, offset2)) {
								continue;
							}
							for (std::int32_t l = 1; l < grid2->width - 1; ++l) {
								if (!R_EqualVert(grid1->verts[static_cast<std::size_t>(k + offset1)], grid2->verts[static_cast<std::size_t>(l + offset2)])) {
									continue;
								}
								grid2->widthLodError[static_cast<std::size_t>(l)] = grid1->widthLodError[static_cast<std::size_t>(k)];
								touch = true;
							}
						}
						for (std::int32_t m = 0; m < 2; ++m) {
							const std::int32_t offset2 = m ? grid2->width - 1 : 0;
							if (R_MergedHeightPoints(grid2, offset2)) {
								continue;
							}
							for (std::int32_t l = 1; l < grid2->height - 1; ++l) {
								if (!R_EqualVert(grid1->verts[static_cast<std::size_t>(k + offset1)], grid2->verts[static_cast<std::size_t>(grid2->width * l + offset2)])) {
									continue;
								}
								grid2->heightLodError[static_cast<std::size_t>(l)] = grid1->widthLodError[static_cast<std::size_t>(k)];
								touch = true;
							}
						}
					}
				}
				for (std::int32_t n = 0; n < 2; ++n) {
					const std::int32_t offset1 = n ? grid1->width - 1 : 0;
					if (R_MergedHeightPoints(grid1, offset1)) {
						continue;
					}
					for (std::int32_t k = 1; k < grid1->height - 1; ++k) {
						for (std::int32_t m = 0; m < 2; ++m) {
							const std::int32_t offset2 = m ? (grid2->height - 1) * grid2->width : 0;
							if (R_MergedWidthPoints(grid2, offset2)) {
								continue;
							}
							for (std::int32_t l = 1; l < grid2->width - 1; ++l) {
								if (!R_EqualVert(grid1->verts[static_cast<std::size_t>(grid1->width * k + offset1)], grid2->verts[static_cast<std::size_t>(l + offset2)])) {
									continue;
								}
								grid2->widthLodError[static_cast<std::size_t>(l)] = grid1->heightLodError[static_cast<std::size_t>(k)];
								touch = true;
							}
						}
						for (std::int32_t m = 0; m < 2; ++m) {
							const std::int32_t offset2 = m ? grid2->width - 1 : 0;
							if (R_MergedHeightPoints(grid2, offset2)) {
								continue;
							}
							for (std::int32_t l = 1; l < grid2->height - 1; ++l) {
								if (!R_EqualVert(grid1->verts[static_cast<std::size_t>(grid1->width * k + offset1)], grid2->verts[static_cast<std::size_t>(grid2->width * l + offset2)])) {
									continue;
								}
								grid2->heightLodError[static_cast<std::size_t>(l)] = grid1->heightLodError[static_cast<std::size_t>(k)];
								touch = true;
							}
						}
					}
				}
				if (touch) {
					grid2->lodFixed = 2;
					R_FixSharedVertexLodError_r(start, grid2);
				}
			}
		}

		void R_FixSharedVertexLodError()
		{
			for (std::int32_t i = 0; i < s_worldData.numsurfaces; ++i) {
				auto grid1Ref = std::dynamic_pointer_cast<srfGridMesh_t>(s_worldData.surfaces[i].data);
				if (!grid1Ref) {
					continue;
				}
				srfGridMesh_t *grid1 = grid1Ref.get();
				if (grid1->surfaceType != SF_GRID) {
					continue;
				}
				if (grid1->lodFixed) {
					continue;
				}
				grid1->lodFixed = 2;
				R_FixSharedVertexLodError_r(i + 1, grid1);
			}
		}

		void R_SetParent(mnode_t *node, mnode_t *parent)
		{
			node->parent = parent;
			if (node->contents != CONTENTS_NODE) {
				return;
			}
			R_SetParent(node->children[0], node);
			R_SetParent(node->children[1], node);
		}

		void R_LoadEntities(lump_t *lump)
		{
			char *p;
			std::string token;
			std::string keyname;
			std::string value;
			world_t *w;

			w = &s_worldData;
			ERR_FAIL_COND_MSG(lump->filelen < 0 || lump->fileofs < 0,
				vformat("LoadMap: missing entities lump in %s", s_worldData.name));
			w->lightGridSize[0] = 64.0f;
			w->lightGridSize[1] = 64.0f;
			w->lightGridSize[2] = 128.0f;

			p = reinterpret_cast<char *>(const_cast<std::uint8_t *>(fileBase + lump->fileofs));

			w->entityData = std::make_unique<char[]>(static_cast<std::size_t>(lump->filelen) + 1U);
			std::memcpy(w->entityData.get(), p, static_cast<std::size_t>(lump->filelen));
			w->entityData[static_cast<std::size_t>(lump->filelen)] = '\0';
			w->entityString = w->entityData.get();
			w->entityParsePoint = w->entityString;

			p = w->entityString;
			token = COM_ParseExt(&p, true);
			if (token.empty() || token[0] != '{') {
				return;
			}

			while (true) {
				token = COM_ParseExt(&p, true);
				if (token.empty() || token[0] == '}') {
					break;
				}
				keyname = token;

				token = COM_ParseExt(&p, true);
				if (token.empty() || token[0] == '}') {
					break;
				}
				value = token;

				if (keyname.compare(0, 17, "vertexremapshader") == 0) {
					const std::size_t semi = value.find(';');
					if (semi == std::string::npos) {
						break;
					}
					std::string oldShader = value.substr(0, semi);
					std::string newShader = value.substr(semi + 1);
					if (tr.vertexLight) {
						R_RemapShader(oldShader.c_str(), newShader.c_str(), "0");
					}
					continue;
				}

				if (keyname.compare(0, 11, "remapshader") == 0) {
					const std::size_t semi = value.find(';');
					if (semi == std::string::npos) {
						break;
					}
					std::string oldShader = value.substr(0, semi);
					std::string newShader = value.substr(semi + 1);
					R_RemapShader(oldShader.c_str(), newShader.c_str(), "0");
					continue;
				}

				if (keyname == "gridsize") {
					std::sscanf(value.c_str(), "%f %f %f", &w->lightGridSize[0], &w->lightGridSize[1], &w->lightGridSize[2]);
					continue;
				}
			}
		}

		void R_LoadShaders(lump_t *lump)
		{
			auto &out = s_worldData.shaders;
			auto &count = s_worldData.numShaders;

			ERR_FAIL_COND_MSG(lump->filelen % sizeof(dshader_t) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			count = lump->filelen / sizeof(dshader_t);
			out = std::make_unique<dshader_t[]>(count);

			std::memcpy(out.get(), fileBase + lump->fileofs, count * sizeof(dshader_t));

			for (std::int32_t i = 0; i < count; ++i) {
				out[i].surfaceFlags = LittleLong(out[i].surfaceFlags);
				out[i].contentFlags = LittleLong(out[i].contentFlags);
			}
		}

		void R_LoadLightmaps(lump_t *lump)
		{
			constexpr std::int32_t LIGHTMAP_SIZE = 128;
			constexpr std::int32_t LIGHTMAP_RGB_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3;
			constexpr std::int32_t LIGHTMAP_RGBA_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4;
			std::uint8_t *buf;
			std::uint8_t *buf_p;
			std::int32_t len;
			std::array<std::uint8_t, LIGHTMAP_RGBA_BYTES> image {};

			len = lump->filelen;
			if (!len) {
				return;
			}
			buf = const_cast<std::uint8_t *>(fileBase + lump->fileofs);

			ERR_FAIL_COND_MSG(len % LIGHTMAP_RGB_BYTES != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			const std::int32_t fileLightmaps = len / LIGHTMAP_RGB_BYTES;
			s_worldData.numLightmaps = fileLightmaps;
			if (s_worldData.numLightmaps == 1) {
				++s_worldData.numLightmaps;
			}

			s_worldData.lightmaps.resize(static_cast<std::size_t>(s_worldData.numLightmaps * LIGHTMAP_RGBA_BYTES));

			for (std::int32_t i = 0; i < s_worldData.numLightmaps; ++i) {
				buf_p = buf + (i < fileLightmaps ? i : 0) * LIGHTMAP_RGB_BYTES;
				for (std::int32_t j = 0; j < LIGHTMAP_SIZE * LIGHTMAP_SIZE; ++j) {
					std::uint8_t in[4] = {
						buf_p[j * 3 + 0],
						buf_p[j * 3 + 1],
						buf_p[j * 3 + 2],
						255,
					};
					R_ColorShiftLightingBytes(in, &image[static_cast<std::size_t>(j * 4)]);
					image[static_cast<std::size_t>(j * 4 + 3)] = 255;
				}
				std::memcpy(s_worldData.lightmaps.data() + static_cast<std::size_t>(i * LIGHTMAP_RGBA_BYTES), image.data(), image.size());
			}
		}

		void R_LoadLightGrid(lump_t *lump)
		{
			vec3_t maxs;
			world_t *w;

			w = &s_worldData;

			w->lightGridInverseSize[0] = 1.0f / w->lightGridSize[0];
			w->lightGridInverseSize[1] = 1.0f / w->lightGridSize[1];
			w->lightGridInverseSize[2] = 1.0f / w->lightGridSize[2];

			float *wMins = w->bmodels[0].bounds[0];
			float *wMaxs = w->bmodels[0].bounds[1];

			for (std::int32_t i = 0; i < 3; ++i) {
				w->lightGridOrigin[i] = w->lightGridSize[i] * std::ceil(wMins[i] / w->lightGridSize[i]);
				maxs[i] = w->lightGridSize[i] * std::floor(wMaxs[i] / w->lightGridSize[i]);
				w->lightGridBounds[i] = static_cast<std::int32_t>((maxs[i] - w->lightGridOrigin[i]) / w->lightGridSize[i] + 1.0f);
			}

			const std::int32_t numGridPoints = w->lightGridBounds[0] * w->lightGridBounds[1] * w->lightGridBounds[2];

			if (lump->filelen != numGridPoints * 8) {
				w->lightGridData = nullptr;
				return;
			}

			w->lightgrid.resize(static_cast<std::size_t>(lump->filelen));
			std::memcpy(w->lightgrid.data(), fileBase + lump->fileofs, static_cast<std::size_t>(lump->filelen));
			w->numLightGrid = numGridPoints;
			w->lightGridData = reinterpret_cast<std::byte *>(w->lightgrid.data());

			auto *lightGridBytes = reinterpret_cast<std::uint8_t *>(w->lightGridData);
			for (std::int32_t i = 0; i < numGridPoints; ++i) {
				R_ColorShiftLightingBytes(&lightGridBytes[i * 8], &lightGridBytes[i * 8]);
				R_ColorShiftLightingBytes(&lightGridBytes[i * 8 + 3], &lightGridBytes[i * 8 + 3]);
			}
		}

		void R_LoadPlanes(lump_t *lump)
		{
			auto &out = s_worldData.planes;
			auto &count = s_worldData.numplanes;

			const auto *in = reinterpret_cast<const dplane_t *>(fileBase + lump->fileofs);
			ERR_FAIL_COND_MSG(lump->filelen % sizeof(dplane_t) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			count = lump->filelen / sizeof(dplane_t);
			out = std::make_unique<cplane_t[]>(count * 2);

			for (std::int32_t i = 0; i < count; ++i) {
				int bits = 0;
				for (int j = 0; j < 3; ++j) {
					out[i].normal[j] = LittleFloat(in[i].normal[j]);
					if (out[i].normal[j] < 0.0f) {
						bits |= 1 << j;
					}
				}
				out[i].dist = LittleFloat(in[i].dist);
				out[i].type = PlaneTypeForNormal(out[i].normal);
				out[i].signbits = bits;
			}
		}

		void R_LoadFogs(lump_t *lump, lump_t *brushes, lump_t *brushsides)
		{
			dfog_t *fogs;
			dbrush_t *brushesIn;
			dbrushside_t *sides;
			fog_t *out;
			std::int32_t count;
			std::int32_t brushesCount;
			std::int32_t sidesCount;
			shader_t *shader;
			float d;

			ERR_FAIL_COND_MSG(lump->filelen % sizeof(dfog_t) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			fogs = reinterpret_cast<dfog_t *>(const_cast<std::uint8_t *>(fileBase + lump->fileofs));
			count = lump->filelen / sizeof(*fogs);

			s_worldData.numfogs = count + 1;
			s_worldData.fogs = std::make_unique<fog_t[]>(s_worldData.numfogs);
			out = s_worldData.fogs.get() + 1;

			if (!count) {
				return;
			}

			brushesIn = reinterpret_cast<dbrush_t *>(const_cast<std::uint8_t *>(fileBase + brushes->fileofs));
			ERR_FAIL_COND_MSG(brushes->filelen % sizeof(*brushesIn) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			brushesCount = brushes->filelen / sizeof(*brushesIn);

			sides = reinterpret_cast<dbrushside_t *>(const_cast<std::uint8_t *>(fileBase + brushsides->fileofs));
			ERR_FAIL_COND_MSG(brushsides->filelen % sizeof(*sides) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			sidesCount = brushsides->filelen / sizeof(*sides);

			for (std::int32_t i = 0; i < count; ++i, ++fogs, ++out) {
				out->originalBrushNumber = LittleLong(fogs->brushNum);
				ERR_FAIL_COND_MSG(static_cast<std::uint32_t>(out->originalBrushNumber) >= static_cast<std::uint32_t>(brushesCount), "fog brushNumber out of range");

				dbrush_t *brush = brushesIn + out->originalBrushNumber;
				const std::int32_t firstSide = LittleLong(brush->firstSide);
				ERR_FAIL_COND_MSG(static_cast<std::uint32_t>(firstSide) > static_cast<std::uint32_t>(sidesCount - 6), "fog brush sideNumber out of range");

				for (std::int32_t j = 0; j < 3; ++j) {
					std::int32_t planeNum = LittleLong(sides[firstSide + j * 2 + 0].planeNum);
					out->bounds[0][j] = -s_worldData.planes[planeNum].dist;
					planeNum = LittleLong(sides[firstSide + j * 2 + 1].planeNum);
					out->bounds[1][j] = s_worldData.planes[planeNum].dist;
				}

				shader = R_FindShader(fogs->shader, LIGHTMAP_NONE, true);
				out->parms = shader->fogParms;
				out->colorInt = ColorBytes4(
					shader->fogParms.color[0],
					shader->fogParms.color[1],
					shader->fogParms.color[2],
					1.0f);

				d = shader->fogParms.depthForOpaque < 1.0f ? 1.0f : shader->fogParms.depthForOpaque;
				out->tcScale = 1.0f / (d * 8.0f);

				const std::int32_t sideNum = LittleLong(fogs->visibleSide);
				if (sideNum == -1) {
					out->hasSurface = false;
				} else {
					out->hasSurface = true;
					const std::int32_t planeNum = LittleLong(sides[firstSide + sideNum].planeNum);
					out->surface[0] = -s_worldData.planes[planeNum].normal[0];
					out->surface[1] = -s_worldData.planes[planeNum].normal[1];
					out->surface[2] = -s_worldData.planes[planeNum].normal[2];
					out->surface[3] = -s_worldData.planes[planeNum].dist;
				}
			}
		}

		void R_LoadSurfaces(lump_t *surfs, lump_t *verts, lump_t *indexLump)
		{
			auto &surfaces = s_worldData.surfaces;
			auto &numsurfaces = s_worldData.numsurfaces;

			int numFaces = 0;
			int numMeshes = 0;
			int numTriSurfs = 0;
			int numFlares = 0;

			auto *in = reinterpret_cast<const dsurface_t *>(fileBase + surfs->fileofs);
			ERR_FAIL_COND_MSG(surfs->filelen % sizeof(*in) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			numsurfaces = surfs->filelen / sizeof(*in);

			auto *dv = reinterpret_cast<drawVert_t *>(const_cast<std::uint8_t *>(fileBase + verts->fileofs));
			ERR_FAIL_COND_MSG(verts->filelen % sizeof(*dv) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			auto *indexes = reinterpret_cast<std::int32_t *>(const_cast<std::uint8_t *>(fileBase + indexLump->fileofs));
			ERR_FAIL_COND_MSG(indexLump->filelen % sizeof(*indexes) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			surfaces = std::make_unique<msurface_t[]>(numsurfaces);
			auto *surface = surfaces.get();

			for (std::int32_t i = 0; i < numsurfaces; ++i, ++in, ++surface) {
				switch (LittleLong(in->surfaceType)) {
					case MST_PATCH:
						ParseMesh(in, dv, surface);
						++numMeshes;
						break;
					case MST_TRIANGLE_SOUP:
						ParseTriSurf(in, dv, surface, indexes);
						++numTriSurfs;
						break;
					case MST_PLANAR:
						ParseFace(in, dv, surface, indexes);
						++numFaces;
						break;
					case MST_FLARE:
						ParseFlare(in, dv, surface, indexes);
						++numFlares;
						break;
					default:
						ERR_FAIL_MSG("Bad surfaceType");
				}
			}

			R_FixSharedVertexLodError();

		}

		void R_LoadMarksurfaces(lump_t *lump)
		{
			std::int32_t *in;
			msurface_t **out;
			std::int32_t count;

			in = reinterpret_cast<std::int32_t *>(const_cast<std::uint8_t *>(fileBase + lump->fileofs));
			ERR_FAIL_COND_MSG(lump->filelen % sizeof(*in) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));
			count = lump->filelen / sizeof(*in);
			s_worldData.marksurfaces = std::make_unique<msurface_t *[]>(count);
			s_worldData.nummarksurfaces = count;
			out = s_worldData.marksurfaces.get();

			for (std::int32_t i = 0; i < count; ++i) {
				const std::int32_t j = LittleLong(in[i]);
				out[i] = s_worldData.surfaces.get() + j;
			}
		}

		void R_LoadNodesAndLeafs(lump_t *nodeLump, lump_t *leafLump)
		{
			dnode_t *in;
			dleaf_t *inLeaf;
			mnode_t *out;
			std::int32_t numNodes;
			std::int32_t numLeafs;

			in = reinterpret_cast<dnode_t *>(const_cast<std::uint8_t *>(fileBase + nodeLump->fileofs));
			ERR_FAIL_COND_MSG(nodeLump->filelen % sizeof(dnode_t) != 0 || leafLump->filelen % sizeof(dleaf_t) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			numNodes = nodeLump->filelen / sizeof(dnode_t);
			numLeafs = leafLump->filelen / sizeof(dleaf_t);

			s_worldData.nodes = std::make_unique<mnode_t[]>(numNodes + numLeafs);
			s_worldData.numnodes = numNodes + numLeafs;
			s_worldData.numDecisionNodes = numNodes;
			out = s_worldData.nodes.get();

			for (std::int32_t i = 0; i < numNodes; ++i, ++in, ++out) {
				for (std::int32_t j = 0; j < 3; ++j) {
					out->mins[j] = static_cast<float>(LittleLong(in->mins[j]));
					out->maxs[j] = static_cast<float>(LittleLong(in->maxs[j]));
				}

				std::int32_t p = LittleLong(in->planeNum);
				out->plane = s_worldData.planes.get() + p;
				out->contents = CONTENTS_NODE;

				for (std::int32_t j = 0; j < 2; ++j) {
					p = LittleLong(in->children[j]);
					if (p >= 0) {
						out->children[j] = s_worldData.nodes.get() + p;
					} else {
						out->children[j] = s_worldData.nodes.get() + numNodes + (-1 - p);
					}
				}
			}

			inLeaf = reinterpret_cast<dleaf_t *>(const_cast<std::uint8_t *>(fileBase + leafLump->fileofs));
			for (std::int32_t i = 0; i < numLeafs; ++i, ++inLeaf, ++out) {
				for (std::int32_t j = 0; j < 3; ++j) {
					out->mins[j] = static_cast<float>(LittleLong(inLeaf->mins[j]));
					out->maxs[j] = static_cast<float>(LittleLong(inLeaf->maxs[j]));
				}

				out->cluster = LittleLong(inLeaf->cluster);
				out->area = LittleLong(inLeaf->area);
				if (out->cluster >= s_worldData.numClusters) {
					s_worldData.numClusters = out->cluster + 1;
				}

				out->firstmarksurface = s_worldData.marksurfaces.get() + LittleLong(inLeaf->firstLeafSurface);
				out->nummarksurfaces = LittleLong(inLeaf->numLeafSurfaces);
			}

			R_SetParent(s_worldData.nodes.get(), nullptr);
		}

		void R_LoadSubmodels(lump_t *lump)
		{
			dmodel_t *in;
			bmodel_t *out;
			std::int32_t count;

			in = reinterpret_cast<dmodel_t *>(const_cast<std::uint8_t *>(fileBase + lump->fileofs));
			ERR_FAIL_COND_MSG(lump->filelen % sizeof(*in) != 0,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			count = lump->filelen / sizeof(*in);
			s_worldData.bmodels = std::make_unique<bmodel_t[]>(count);
			out = s_worldData.bmodels.get();

			for (std::int32_t i = 0; i < count; ++i, ++in, ++out) {
				for (std::int32_t j = 0; j < 3; ++j) {
					out->bounds[0][j] = LittleFloat(in->mins[j]);
					out->bounds[1][j] = LittleFloat(in->maxs[j]);
				}

				out->firstSurface = s_worldData.surfaces.get() + LittleLong(in->firstSurface);
				out->numSurfaces = LittleLong(in->numSurfaces);
			}
		}

		void R_LoadVisibility(lump_t *lump)
		{
			std::int32_t len;
			std::uint8_t *buf;

			len = (s_worldData.numClusters + 63) & ~63;
			s_worldData.novisData.assign(static_cast<std::size_t>(len), std::byte { 0xff });
			s_worldData.novis = s_worldData.novisData.data();

			len = lump->filelen;
			if (!len) {
				return;
			}
			ERR_FAIL_COND_MSG(len < 8,
				vformat("LoadMap: funny lump size in %s", s_worldData.name));

			buf = const_cast<std::uint8_t *>(fileBase + lump->fileofs);
			s_worldData.numClusters = LittleLong(reinterpret_cast<std::int32_t *>(buf)[0]);
			s_worldData.clusterBytes = LittleLong(reinterpret_cast<std::int32_t *>(buf)[1]);

			if (tr.externalVisData) {
				s_worldData.vis = tr.externalVisData;
			} else {
				const auto *vis_begin = reinterpret_cast<const std::byte *>(buf + 8);
				const auto *vis_end = reinterpret_cast<const std::byte *>(buf + len);
				s_worldData.visibility.assign(vis_begin, vis_end);
				s_worldData.vis = s_worldData.visibility.data();
			}
		}

	}

	void RE_SetWorldVisData(const std::byte *vis)
	{
		tr.externalVisData = vis;
	}

	void RE_ClearWorldMap()
	{
		tr.world = nullptr;
		tr.worldMapLoaded = false;
		fileBase = nullptr;
		c_gridVerts = 0;
		s_worldData = world_t {};
	}

	world_t *R_GetWorld()
	{
		return tr.world;
	}

	bool R_GetEntityToken(char *buffer, std::int32_t size)
	{
		std::string s;

		ERR_FAIL_COND_V_MSG(!buffer || size <= 0, false, "R_GetEntityToken: bad output buffer.");

		s = COM_ParseExt(&s_worldData.entityParsePoint, true);
		std::snprintf(buffer, static_cast<std::size_t>(size), "%s", s.c_str());
		if (!s_worldData.entityParsePoint || s.empty()) {
			s_worldData.entityParsePoint = s_worldData.entityString;
			return false;
		}

		return true;
	}

	void RE_LoadWorldMap(const char *name)
	{
		std::size_t i;
		std::unique_ptr<std::uint8_t[]> bytes;
		FilePtr file(std::fopen(name, "rb"), &std::fclose);
		long end = 0;

		if (tr.worldMapLoaded) {
			RE_ClearWorldMap();
		}

		tr.sunDirection[0] = 0.45f;
		tr.sunDirection[1] = 0.3f;
		tr.sunDirection[2] = 0.9f;
		VectorNormalize(tr.sunDirection);

		tr.world = nullptr;

		s_worldData = world_t {};
		std::snprintf(s_worldData.name, sizeof(s_worldData.name), "%s", name);
		const char *base_name = std::strrchr(name, '/');
		if (!base_name) {
			base_name = std::strrchr(name, '\\');
		}
		base_name = base_name ? base_name + 1 : name;
		char base_copy[MAX_QPATH];
		std::snprintf(base_copy, sizeof(base_copy), "%s", base_name);
		char *dot = std::strrchr(base_copy, '.');
		if (dot) {
			*dot = '\0';
		}
		std::snprintf(s_worldData.baseName, sizeof(s_worldData.baseName), "%s", base_copy);

		ERR_FAIL_COND_MSG(!file, "Could not open BSP file.");

		ERR_FAIL_COND_MSG(std::fseek(file.get(), 0, SEEK_END) != 0, "Could not read BSP file.");

		end = std::ftell(file.get());
		ERR_FAIL_COND_MSG(end < static_cast<long>(sizeof(dheader_t)), "File is too small to contain a Quake 3 BSP header.");
		ERR_FAIL_COND_MSG(end > LONG_MAX, "BSP file is too large.");

		bytes = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(end));
		ERR_FAIL_COND_MSG(std::fseek(file.get(), 0, SEEK_SET) != 0, "Could not read BSP file.");
		ERR_FAIL_COND_MSG(std::fread(bytes.get(), 1, static_cast<std::size_t>(end), file.get()) != static_cast<std::size_t>(end), "Could not read BSP file.");

		auto *header = reinterpret_cast<dheader_t *>(bytes.get());
		ERR_FAIL_COND_MSG(LittleLong(header->ident) != BSP_IDENT, "File is not a Quake 3 IBSP file.");
		ERR_FAIL_COND_MSG(LittleLong(header->version) != BSP_VERSION, "Unsupported BSP version.");

		c_gridVerts = 0;
		for (i = 0; i < sizeof(dheader_t) / sizeof(std::int32_t); ++i) {
			reinterpret_cast<std::int32_t *>(header)[i] = LittleLong(reinterpret_cast<std::int32_t *>(header)[i]);
		}

		fileBase = bytes.get();

		R_LoadShaders(&header->lumps[LUMP_SHADERS]);
		R_LoadLightmaps(&header->lumps[LUMP_LIGHTMAPS]);
		R_LoadPlanes(&header->lumps[LUMP_PLANES]);
		R_LoadFogs(&header->lumps[LUMP_FOGS], &header->lumps[LUMP_BRUSHES], &header->lumps[LUMP_BRUSHSIDES]);
		R_LoadSurfaces(&header->lumps[LUMP_SURFACES], &header->lumps[LUMP_DRAWVERTS], &header->lumps[LUMP_DRAWINDEXES]);
		R_LoadMarksurfaces(&header->lumps[LUMP_LEAFSURFACES]);
		R_LoadNodesAndLeafs(&header->lumps[LUMP_NODES], &header->lumps[LUMP_LEAFS]);
		R_LoadSubmodels(&header->lumps[LUMP_MODELS]);
		R_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
		R_LoadEntities(&header->lumps[LUMP_ENTITIES]);
		R_LoadLightGrid(&header->lumps[LUMP_LIGHTGRID]);

		s_worldData.dataSize = static_cast<std::int32_t>(end);
		tr.world = &s_worldData;
		tr.worldMapLoaded = true;
		fileBase = nullptr;
	}
}
