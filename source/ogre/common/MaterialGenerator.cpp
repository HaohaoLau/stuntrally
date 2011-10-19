#include "pch.h"
#include "../Defines.h"

#include "MaterialGenerator.h"
#include "MaterialDefinition.h"
#include "MaterialFactory.h"

#ifndef ROAD_EDITOR
	#include "../OgreGame.h"
#else
	#include "../../editor/OgreApp.h"
#endif

#include <OgreMaterialManager.h>
#include <OgreTechnique.h>
#include <OgrePass.h>
#include <OgreTextureUnitState.h>
#include <OgreHighLevelGpuProgramManager.h>
#include <OgreHighLevelGpuProgram.h>
#include <OgreGpuProgramParams.h>
//#include <OgreEntity.h>
//#include <OgreSubEntity.h>
//#include <OgreSceneManager.h>
using namespace Ogre;

void MaterialGenerator::generate(bool fixedFunction)
{	
	MaterialPtr mat = prepareMaterial(mDef->getName());
	
	// reset some attributes
	mDiffuseTexUnit = 0; mNormalTexUnit = 0; mEnvTexUnit = 0;
	mShadowTexUnit_start = 0; mTexUnit_i = 0;
	
	// test
	//mParent->setShaders(false);
	//mParent->setEnvMap(false);
	
	// only 1 technique
	Ogre::Technique* technique = mat->createTechnique();
	
	// single pass
	Ogre::Pass* pass = technique->createPass();
	
	pass->setAmbient( mDef->mProps->ambient.x, mDef->mProps->ambient.y, mDef->mProps->ambient.z );
	pass->setDiffuse( mDef->mProps->diffuse.x, mDef->mProps->diffuse.y, mDef->mProps->diffuse.z, mDef->mProps->diffuse.w );
	
	if (!mParent->getShaders() || fixedFunction)
	{
		pass->setSpecular(mDef->mProps->specular.x, mDef->mProps->specular.y, mDef->mProps->specular.z, 1.0 );
		pass->setShininess(mDef->mProps->specular.w);
	}
	else
	{
		// shader assumes matShininess in specular w component
		pass->setSpecular(mDef->mProps->specular.x, mDef->mProps->specular.y, mDef->mProps->specular.z, mDef->mProps->specular.w);
	}
	
	std::string diffuseMap = pickTexture(&mDef->mProps->diffuseMaps);
	std::string normalMap = pickTexture(&mDef->mProps->normalMaps);
	
	// test
	//pass->setCullingMode(CULL_NONE);
	//pass->setShadingMode(SO_PHONG);
	
	if (!mParent->getShaders() || fixedFunction)
	{
		pass->setShadingMode(SO_PHONG);
		
		// diffuse map
		Ogre::TextureUnitState* tu = pass->createTextureUnitState( diffuseMap );
		
		if (needEnvMap())
		{
			// env map
			tu = pass->createTextureUnitState();
			tu->setCubicTextureName( mDef->mProps->envMap, true );
			tu->setEnvironmentMap(true, TextureUnitState::ENV_REFLECTION);
			tu->setTextureAddressingMode(TextureUnitState::TAM_CLAMP);
			
			// blend with diffuse map using 'reflection amount' property
			tu->setColourOperationEx(LBX_BLEND_MANUAL, LBS_CURRENT, LBS_TEXTURE, 
									ColourValue::White, ColourValue::White, 1-mDef->mProps->reflAmount);
		}
	}
	else
	{
		// diffuse map
		Ogre::TextureUnitState* tu = pass->createTextureUnitState( diffuseMap );
		tu->setName("diffuseMap");
		mDiffuseTexUnit = 0; mTexUnit_i++;
		
		// env map
		if (needEnvMap())
		{
			tu = pass->createTextureUnitState( mDef->mProps->envMap );
			tu->setName("envMap");
			tu->setTextureAddressingMode(TextureUnitState::TAM_CLAMP);
			mEnvTexUnit = mTexUnit_i; mTexUnit_i++;
		}
		
		// normal map
		if (needNormalMap())
		{
			tu = pass->createTextureUnitState( normalMap );
			tu->setName("normalMap");
			mNormalTexUnit = mTexUnit_i; mTexUnit_i++;
		}
		
		// shadow maps
		if (needShadows())
		{
			mShadowTexUnit_start = mTexUnit_i;
			for (int i = 0; i < mParent->getNumShadowTex(); ++i)
			{
				tu = pass->createTextureUnitState();
				tu->setName("shadowMap" + toStr(i));
				tu->setContentType(TextureUnitState::CONTENT_SHADOW);
				tu->setTextureAddressingMode(TextureUnitState::TAM_BORDER);
				tu->setTextureBorderColour(ColourValue::White);
				mTexUnit_i++;
			}
		}
		
		// create shaders
		HighLevelGpuProgramPtr fragmentProg, vertexProg;
		try
		{
			vertexProg = createVertexProgram();
			fragmentProg = createFragmentProgram();
		}
		catch (Ogre::Exception& e) {
			LogO(e.getFullDescription());
		}
		
		if (fragmentProg.isNull() || vertexProg.isNull() || 
			!fragmentProg->isSupported() || !vertexProg->isSupported())
		{
			LogO("[MaterialFactory] WARNING: shader for material '" + mDef->getName()
				+ "' is not supported, falling back to fixed-function");
			generate(true);
			return;
		}
		
		pass->setVertexProgram(vertexProg->getName());
		pass->setFragmentProgram(fragmentProg->getName());
	}
}

//----------------------------------------------------------------------------------------

MaterialPtr MaterialGenerator::prepareMaterial(const std::string& name)
{
	MaterialPtr mat;
	if (MaterialManager::getSingleton().resourceExists(name))
	{
		mat = MaterialManager::getSingleton().getByName(name);
		//MaterialManager::getSingleton().remove(name);
		mat->removeAllTechniques();
	}
	else
	{
		mat = MaterialManager::getSingleton().create(name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
	}
	return mat;
}

//----------------------------------------------------------------------------------------

inline bool MaterialGenerator::needShadows()
{
	return (mDef->mProps->receivesShadows && mParent->getShadows())
		|| (mDef->mProps->receivesDepthShadows && mParent->getShadowsDepth());
	//!todo shadow priority
}

inline bool MaterialGenerator::needNormalMap()
{
	return (mDef->mProps->normalMaps.size() > 0) && mParent->getNormalMap();
	//!todo normal map priority
}

inline bool MaterialGenerator::needEnvMap()
{
	return (mDef->mProps->envMap != "") && mParent->getEnvMap();
	//!todo env map priority
}

inline bool MaterialGenerator::fpNeedWsNormal()
{
	return needEnvMap();
	//!todo
}

inline bool MaterialGenerator::fpNeedEyeVector()
{
	return needEnvMap();
	//!todo
}

std::string MaterialGenerator::getChannel(unsigned int n)
{
	if (n == 0) 		return "x";
	else if (n == 1) 	return "y";
	else if (n == 2)	return "z";
	else 				return "w";
}

//----------------------------------------------------------------------------------------

std::string MaterialGenerator::pickTexture(textureMap* textures)
{
	if (textures->size() == 0) return "";
	
	// we assume the textures are sorted by size
	textureMap::iterator it;
	for (it = textures->begin(); it != textures->end(); ++it)
	{
		if ( it->first < mParent->getTexSize() ) continue;
		/* else */ break;
	}
	
	if (it == textures->end()) --it;
	
	return it->second;
}


//----------------------------------------------------------------------------------------

HighLevelGpuProgramPtr MaterialGenerator::createVertexProgram()
{
	HighLevelGpuProgramManager& mgr = HighLevelGpuProgramManager::getSingleton();
	std::string progName = mDef->getName() + "_VP";

	HighLevelGpuProgramPtr ret = mgr.getByName(progName);
	if (!ret.isNull())
		mgr.remove(progName);

	ret = mgr.createProgram(progName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, 
		"cg", GPT_VERTEX_PROGRAM);

	ret->setParameter("profiles", "vs_1_1 arbvp1");
	ret->setParameter("entry_point", "main_vp");

	StringUtil::StrStreamType sourceStr;
	generateVertexProgramSource(sourceStr);
	LogO("Vertex program source:\n");
	LogO(sourceStr.str());
	ret->setSource(sourceStr.str());
	ret->load();
	vertexProgramParams(ret);
	
	return ret;
}

//----------------------------------------------------------------------------------------

void MaterialGenerator::generateVertexProgramSource(Ogre::StringUtil::StrStreamType& outStream)
{
	//!todo more optizations
	outStream << 
		"void main_vp( "
		"	float2 texCoord 					: TEXCOORD0,"
		"	float4 position 					: POSITION,"
		"	float3 normal			 			: NORMAL,"
		"	uniform float4 eyePosition,					 "
		"	out float4 oPosition			 	: POSITION, \n"
	; if (!needShadows()) outStream <<
		"	out float2 oTexCoord	 			: TEXCOORD0,"
	; else outStream <<
		"	out float3 oTexCoord				: TEXCOORD0,"
		
	; if (fpNeedWsNormal()) outStream << 
		"	out float3 oWsNormal  				: TEXCOORD1,"
	; outStream <<
		"	out	float4	oTangentToCubeSpace0	: TEXCOORD2,"
		"	out	float4	oTangentToCubeSpace1	: TEXCOORD3,"
		"	out	float4	oTangentToCubeSpace2	: TEXCOORD4, \n"	
	;
	
	if (needShadows()) {
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
		{
			outStream << "out float4 oLightPosition"+toStr(i)+" : TEXCOORD"+toStr(i+5)+",";
		}
		outStream << "\n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
		{
			outStream << "uniform float4x4 texWorldViewProjMatrix"+toStr(i)+", ";
		}
		outStream << "\n";
	}

		if (fpNeedWsNormal())
			outStream <<
		"	uniform float4x4 wITMat,"
		; outStream << 
		"	uniform float4x4 wvpMat,"
		"	uniform float4x4 wMat"
		") \n"
		"{ \n"
		"	oPosition = mul(wvpMat, position); \n"
		
	; if (needShadows()) {
		 outStream <<
		"	oTexCoord.xy = texCoord; \n"
		"	oTexCoord.z = oPosition.z; \n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
		{
			outStream << "oLightPosition"+toStr(i)+" = mul(texWorldViewProjMatrix"+toStr(i)+", position); \n";
		}
	}
	
	else outStream <<
		"	oTexCoord = texCoord; \n"
		
	; if (fpNeedWsNormal()) outStream <<
		"	oWsNormal = mul( (float3x3) wITMat, normal ); \n"
	; outStream <<
		"	float3 eyeVector = mul( wMat, position ) - eyePosition; \n" // transform eye into view space
		"	oTangentToCubeSpace0.w = eyeVector.x; \n" // store eye vector
		"	oTangentToCubeSpace1.w = eyeVector.y; \n"
		"	oTangentToCubeSpace2.w = eyeVector.z; \n"
		"} \n";
}

//----------------------------------------------------------------------------------------

void MaterialGenerator::vertexProgramParams(HighLevelGpuProgramPtr program)
{
	GpuProgramParametersSharedPtr params = program->getDefaultParameters();
	//params->setIgnoreMissingParams(true);
	params->setNamedAutoConstant("wMat", GpuProgramParameters::ACT_WORLD_MATRIX);
	params->setNamedAutoConstant("wvpMat", GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
	if (fpNeedWsNormal())
		params->setNamedAutoConstant("wITMat", GpuProgramParameters::ACT_INVERSE_TRANSPOSE_WORLD_MATRIX);
	params->setNamedAutoConstant("eyePosition", GpuProgramParameters::ACT_CAMERA_POSITION);
	
	if (needShadows())
	for (int i=0; i<mParent->getNumShadowTex(); ++i)
	{
		params->setNamedAutoConstant("texWorldViewProjMatrix"+toStr(i), GpuProgramParameters::ACT_TEXTURE_WORLDVIEWPROJ_MATRIX, i);
	}
}

//----------------------------------------------------------------------------------------

HighLevelGpuProgramPtr MaterialGenerator::createFragmentProgram()
{
	HighLevelGpuProgramManager& mgr = HighLevelGpuProgramManager::getSingleton();
	std::string progName = mDef->getName() + "_FP";

	HighLevelGpuProgramPtr ret = mgr.getByName(progName);
	if (!ret.isNull())
		mgr.remove(progName);

	ret = mgr.createProgram(progName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, 
			"cg", GPT_FRAGMENT_PROGRAM);

	ret->setParameter("profiles", "ps_2_x arbfp1");
	ret->setParameter("entry_point", "main_fp");

	StringUtil::StrStreamType sourceStr;
	generateFragmentProgramSource(sourceStr);
	LogO("Fragment program source:\n");
	LogO(sourceStr.str());
	ret->setSource(sourceStr.str());
	ret->load();
	fragmentProgramParams(ret);
	
	return ret;
}

//----------------------------------------------------------------------------------------

void MaterialGenerator::generateFragmentProgramSource(Ogre::StringUtil::StrStreamType& outStream)
{
	if (needShadows())
	{
		/// shadow helper functions
		// 2x2 pcf
		outStream <<
		"float shadowPCF(sampler2D shadowMap, float4 shadowMapPos, float2 offset)"
		"{ \n"
		"	shadowMapPos = shadowMapPos / shadowMapPos.w; \n"
		"	float2 uv = shadowMapPos.xy; \n"
		"	float3 o = float3(offset, -offset.x) * 0.3f; \n"

		"	float c =	(shadowMapPos.z <= tex2D(shadowMap, uv.xy - o.xy).r) ? 1 : 0; \n"
		"	c +=		(shadowMapPos.z <= tex2D(shadowMap, uv.xy + o.xy).r) ? 1 : 0; \n"
		"	c +=		(shadowMapPos.z <= tex2D(shadowMap, uv.xy + o.zy).r) ? 1 : 0; \n"
		"	c +=		(shadowMapPos.z <= tex2D(shadowMap, uv.xy - o.zy).r) ? 1 : 0; \n"
		"	return c / 4;  \n"
		"} \n"
		;
		
		// pssm
		outStream <<
		"float calcPSSMShadow(";
		
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			outStream << "sampler2D shadowMap"+toStr(i)+", ";
		outStream << "\n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			outStream << "float4 lsPos"+toStr(i)+", ";
		outStream << "\n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			outStream << "float4 invShadowMapSize"+toStr(i)+", ";
		outStream << "\n";
		
		outStream <<
		"	float4 pssmSplitPoints, float camDepth"
		") \n"
		"{ \n"
		"	float shadow; \n"
		
		; for (int i=0; i<mParent->getNumShadowTex(); ++i)
		{
			if (i==0)
				outStream << "if (camDepth <= pssmSplitPoints.y) \n";
			else if (i < mParent->getNumShadowTex()-1)
				outStream << "else if (camDepth <= pssmSplitPoints."+getChannel(i+1)+") \n";
			else
				outStream << "else \n";
				
			outStream <<
			"{ \n"
			"	shadow = shadowPCF(shadowMap"+toStr(i)+", lsPos"+toStr(i)+", invShadowMapSize"+toStr(i)+".xy); \n"
			"} \n";
		}
		
		outStream <<
		"	return shadow; \n"
		"} \n"
		;
	}
	
	outStream <<
		"void main_fp("
	; if (!needShadows()) outStream <<
		"	in float2 texCoord : TEXCOORD0,"
	; else outStream <<
		"	in float3 texCoord : TEXCOORD0,"
	; if (fpNeedWsNormal()) outStream <<
		"	in float3 wsNormal : TEXCOORD1,"
	; outStream <<
		"	in float4 tangentToCubeSpace0 : TEXCOORD2,"
		"	in float4 tangentToCubeSpace1 : TEXCOORD3,"
		"	in float4 tangentToCubeSpace2 : TEXCOORD4, \n"
		
		"	uniform sampler2D diffuseMap : TEXUNIT"+toStr(mDiffuseTexUnit)+", \n"
		
	; if (needEnvMap()) outStream << 
		"	uniform samplerCUBE envMap : TEXUNIT"+toStr(mEnvTexUnit)+","
		"	uniform float reflAmount, \n";
	
	if (needShadows())
	{
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
		{
			outStream <<
		"	uniform sampler2D shadowMap"+toStr(i)+" : TEXUNIT"+toStr(mShadowTexUnit_start+i)+", ";
		}
		outStream << "\n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			outStream << "in float4 lightPosition"+toStr(i)+" : TEXCOORD"+toStr(i+5)+", ";
		outStream << "\n";
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			outStream << "uniform float4 invShadowMapSize"+toStr(i)+", ";
		outStream << "\n";
		outStream << 
		"	uniform float4 pssmSplitPoints,";
	}
	
	
	outStream << 

		"	out float4 oColor : COLOR"
		") \n"
		"{ \n"
		
	// calc shadowing
	; if (needShadows()) {
		outStream <<
		"	float shadowing;"
		"	shadowing = calcPSSMShadow("
		;
	for (int i=0; i<mParent->getNumShadowTex(); ++i)
		outStream << "shadowMap"+toStr(i)+", ";
	for (int i=0; i<mParent->getNumShadowTex(); ++i)
		outStream << "lightPosition"+toStr(i)+", ";
	for (int i=0; i<mParent->getNumShadowTex(); ++i)
		outStream << "invShadowMapSize"+toStr(i)+", ";
		
		outStream <<
		"pssmSplitPoints, texCoord.z); \n";
		
	}
		
	outStream << 
		"	float3 eyeVector; \n" // fetch view vector
		"	eyeVector.x = tangentToCubeSpace0.w; \n"
		"	eyeVector.y = tangentToCubeSpace1.w; \n"
		"	eyeVector.z = tangentToCubeSpace2.w; \n"
		"	eyeVector = normalize(eyeVector); \n"
	; if (fpNeedWsNormal()) outStream <<
		"	wsNormal = normalize(wsNormal); \n"
		
	; if (needEnvMap()) outStream << 
		"	float3 r; \n" // Calculate reflection vector within world (view) space.
		"	r = reflect( eyeVector, wsNormal ); \n"
		"	float4 envColor = texCUBE(envMap, r); \n"
	; outStream <<
		"	float4 diffuseColor = tex2D(diffuseMap, texCoord); \n"
	
	//!todo
	; if (needEnvMap()) outStream << 
		"	float4 color1 = lerp(diffuseColor, envColor, reflAmount); \n"
		
	; else outStream <<
		"	float4 color1 = diffuseColor; \n"
	
	; if (needShadows()) outStream <<
		"	oColor = color1 * shadowing; \n" // test
	
	; else outStream <<
		"	oColor = color1; \n"
		
	; outStream << 
		"} \n";
}

//----------------------------------------------------------------------------------------

void MaterialGenerator::fragmentProgramParams(HighLevelGpuProgramPtr program)
{
	GpuProgramParametersSharedPtr params = program->getDefaultParameters();
	//params->setIgnoreMissingParams(true);
	if (needEnvMap()) {
		params->setNamedConstant("reflAmount", mDef->mProps->reflAmount);
	}
	if (needShadows()) {
		params->setNamedConstant("pssmSplitPoints", mParent->pApp->splitPoints);
		for (int i=0; i<mParent->getNumShadowTex(); ++i)
			params->setNamedAutoConstant("invShadowMapSize"+toStr(i), GpuProgramParameters::ACT_INVERSE_TEXTURE_SIZE, i);
	}
}
