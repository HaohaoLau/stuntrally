#include "pch.h"
#include "Defines.h"
#include "CarModel.h"
#include "../vdrift/pathmanager.h"
#include "../vdrift/mathvector.h"
#include "../vdrift/track.h"
#include "../vdrift/game.h"
#include "OgreGame.h"
#include "SplitScreen.h"
#include "common/SceneXml.h"
#include "FollowCamera.h"
#include "CarReflection.h"
#include "../road/Road.h"
#include "common/RenderConst.h"

#include "boost/filesystem.hpp"
#define  FileExists(s)  boost::filesystem::exists(s)

#include <OgreRoot.h>
#include <OgreTerrain.h>
#include <OgreEntity.h>
#include <OgreManualObject.h>
#include <OgreMaterialManager.h>
#include <OgreParticleSystem.h>
#include <OgreParticleEmitter.h>
#include <OgreParticleAffector.h>
#include <OgreRibbonTrail.h>

using namespace Ogre;


CarModel::CarModel(unsigned int index, eCarType type, const std::string name,
	Ogre::SceneManager* sceneMgr, SETTINGS* set, GAME* game, Scene* s, Ogre::Camera* cam, App* app) :
	fCam(0), pMainNode(0), pCar(0), terrain(0), resCar(""), mCamera(0), pReflect(0), pApp(app), color(1,0,0)
{
	iIndex = index;  sDirname = name;  pSceneMgr = sceneMgr;
	pSet = set;  pGame = game;  sc = s;  mCamera = cam;  eType = type;
	bGetStPos = true;  fChkTime = 0.f;  iChkWrong = -1;  iWonPlace = 0;
	
	if (type != CT_GHOST)  // ghost has pCar, dont create
	{
		int i = set->car_collis ? iIndex : 0;  //  offset car start pos when cars collide
		MATHVECTOR<float, 3> pos(0,10,0);
		QUATERNION<float> rot;
		pos = pGame->track.GetStart(i).first;
		rot = pGame->track.GetStart(i).second;
		vStartPos = Vector3(pos[0], pos[2], -pos[1]); // save in ogre coords

		//  offset car start pos when cars collide
		MATHVECTOR<float, 3> offset(0,0,0);
		pCar = pGame->LoadCar(sDirname, pos, rot, true, false);
		if (!pCar)  LogO("Error creating car " + sDirname);
	}
	
	for (int w = 0; w < 4; ++w)
	{	ps[w] = 0;  pm[w] = 0;  pd[w] = 0;
		ndWh[w] = 0;  ndWhE[w] = 0; whTrl[w] = 0;
		wht[w] = 0.f;  whTerMtr[w] = 0;  whRoadMtr[w] = 0;  }
	for (int i=0; i < 2; i++)
		pb[i] = 0;
	ph = 0;
}

CarModel::~CarModel()
{
	delete pReflect;  pReflect = 0;
	
	delete fCam;  fCam = 0;
	pSceneMgr->destroyCamera("CarCamera" + toStr(iIndex));
	
	//  hide trails
	for (int w=0; w<4; ++w)  if (whTrl[w])  {	wht[w] = 0.f;
		whTrl[w]->setVisible(false);	whTrl[w]->setInitialColour(0, 0.5,0.5,0.5, 0);	}

	// destroy cloned materials
	for (int i=0; i<NumMaterials; i++)
		Ogre::MaterialManager::getSingleton().remove(sMtr[i]);
	
	// Destroy par sys
	for (int w=0; w < 4; w++)  {
		if (ps[w]) {  pSceneMgr->destroyParticleSystem(ps[w]);   ps[w]=0;  }
		if (pm[w]) {  pSceneMgr->destroyParticleSystem(pm[w]);   pm[w]=0;  }
		if (pd[w]) {  pSceneMgr->destroyParticleSystem(pd[w]);   pd[w]=0;  }  }
	for (int i=0; i < 2; i++)
		if (pb[i]) {  pSceneMgr->destroyParticleSystem(pb[i]);   pb[i]=0;  }
	if (ph)  {  pSceneMgr->destroyParticleSystem(ph);   ph=0;  }
						
	if (pMainNode) pSceneMgr->destroySceneNode(pMainNode);
	
	// Destroy resource group, will also destroy all resources in it
	if (Ogre::ResourceGroupManager::getSingleton().resourceGroupExists("Car" + toStr(iIndex)))
		Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("Car" + toStr(iIndex));
}

void CarModel::setVisible(bool vis)
{
	//if (pMainNode->getVisible() == vis)  return;  //opt..
	pMainNode->setVisible(vis);
	for (int w=0; w < 4; ++w)
		ndWh[w]->setVisible(vis);
	UpdParsTrails(vis);
}


//  Update
//-------------------------------------------------------------------------------------------------------
void CarModel::Update(PosInfo& posInfo, float time)
{	
	if (!posInfo.bNew)  return;  // new only
	posInfo.bNew = false;
	
	if (!pMainNode) return;
	//  car pos and rot
	pMainNode->setPosition(posInfo.pos);
	pMainNode->setOrientation(posInfo.rot);
	

	//  update particle emitters
	//  boost
	if (pSet->particles)
	for (int i=0; i < 2; i++)
	if (pb[i])
	{
		float emitB = posInfo.fboost * 40.f;  // par
		ParticleEmitter* pe = pb[i]->getEmitter(0);
		pe->setEmissionRate(emitB);
	}

	//  world hit
	CARDYNAMICS& cd = pCar->dynamics;
	if (ph && cd.fHitTime > 0.f && pSet->particles)
	{
		ParticleEmitter* pe = ph->getEmitter(0);
		pe->setPosition(cd.vHitPos);
		pe->setDirection(cd.vHitNorm);

		cd.fHitTime -= time*2;
		pe->setEmissionRate(cd.fHitTime > 0.f ? pSet->particles_len * std::min(160.f, cd.fParIntens) * cd.fHitTime : 0);
		pe->setParticleVelocity(cd.fParVel);
	}
	
	//  wheels
	for (int w=0; w < 4; w++)
	{
		float wR = posInfo.whR[w];
		ndWh[w]->setPosition(posInfo.whPos[w]);
		ndWh[w]->setOrientation(posInfo.whRot[w]);

		int whMtr = posInfo.whTerMtr[w];
		int whRd = posInfo.whRoadMtr[w];
		float whVel = posInfo.whVel[w] * 3.6f;  //kmh
		float slide = posInfo.whSlide[w], squeal = posInfo.whSqueal[w];
		float onGr = slide < 0.f ? 0.f : 1.f;

		//  wheel temp
		wht[w] += squeal * time * 7;
		wht[w] -= time*6;  if (wht[w] < 0.f)  wht[w] = 0.f;

		///  emit rates +
		Real emitS = 0.f, emitM = 0.f, emitD = 0.f;  //paused

		if (!pGame->pause)
		{
			 Real sq = squeal* std::min(1.f, wht[w]), l = pSet->particles_len * onGr;
			 emitS = sq * (whVel * 30) * l *0.3f;  //..
			 emitM = slide < 1.4f ? 0.f :  (8.f * sq * std::min(5.f, slide) * l);
			 emitD = (std::min(140.f, whVel) / 3.5f + slide * 1.f ) * l;  
			 //  resume
			 if (pd[w])  {
			 pd[w]->setSpeedFactor(1.f);  ps[w]->setSpeedFactor(1.f);  pm[w]->setSpeedFactor(1.f);
			 if (w < 2)  pb[w]->setSpeedFactor(1.f);  }
		}else{
			 //  stop par sys
			 if (pd[w])  {
			 pd[w]->setSpeedFactor(0.f);  ps[w]->setSpeedFactor(0.f);  pm[w]->setSpeedFactor(0.f);
			 if (w < 2)  pb[w]->setSpeedFactor(0.f);  }
		}
		Real sizeD = (0.3f + 1.1f * std::min(140.f, whVel) / 140.f) * (w < 2 ? 0.5f : 1.f);

		//  ter mtr factors
		int mtr = std::max(0, std::min(whMtr-1, (int)(sc->td.layers.size()-1)));
		TerLayer& lay = whMtr==0 ? sc->td.layerRoad : sc->td.layersAll[sc->td.layers[mtr]];
		emitD *= lay.dust;  emitM *= lay.mud;  sizeD *= lay.dustS;  emitS *= lay.smoke;
		if (whRd == 2)  emitD = 0;  // no dust in pipes

		//  par emit
		Vector3 vpos = posInfo.whPos[w];
		if (pSet->particles)
		{
			if (ps[w] && sc->td.layerRoad.smoke > 0.f/*&& !sc->ter*/)  // only at vdr road
			{
				ParticleEmitter* pe = ps[w]->getEmitter(0);
				pe->setPosition(vpos + posInfo.carY * wR*0.7f); // 0.218
				/**/ps[w]->getAffector(0)->setParameter("alpha", toStr(-0.4f - 0.07f/2.4f * whVel));
				/**/pe->setTimeToLive( std::max(0.1, 2 - whVel/2.4f * 0.04) );  // fade,live
				pe->setDirection(-posInfo.carY);	pe->setEmissionRate(emitS);
			}
			if (pm[w])	//  mud
			{	ParticleEmitter* pe = pm[w]->getEmitter(0);
				//pe->setDimensions(sizeM,sizeM);
				pe->setPosition(vpos + posInfo.carY * wR*0.7f); // 0.218
				pe->setDirection(-posInfo.carY);	pe->setEmissionRate(emitM);
			}
			if (pd[w])	//  dust
			{	pd[w]->setDefaultDimensions(sizeD,sizeD);
				ParticleEmitter* pe = pd[w]->getEmitter(0);
				pe->setPosition(vpos + posInfo.carY * wR*0.51f ); // 0.16
				pe->setDirection(-posInfo.carY);	pe->setEmissionRate(emitD);
			}
		}

		//  update trails h+
		if (pSet->trails)  {
			if (ndWhE[w])
			{	Vector3 vp = vpos + posInfo.carY * wR*0.72f;  // 0.22
				if (terrain && whMtr > 0)
					vp.y = terrain->getHeightAtWorldPosition(vp) + 0.05f;
					//if (/*whOnRoad[w]*/whMtr > 0 && road)  // on road, add ofs
					//	vp.y += road->fHeight;	}/**/
				ndWhE[w]->setPosition(vp);
			}
			float al = 0.5f * /*squeal*/ std::min(1.f, 0.7f * wht[w]) * onGr;  // par+
			if (whTrl[w])	whTrl[w]->setInitialColour(0,
				lay.tclr.r,lay.tclr.g,lay.tclr.b, lay.tclr.a * al/**/);
		}
	}

	// Reflection
	pReflect->camPosition = pMainNode->getPosition();
	
	// blendmaps
	UpdWhTerMtr();
}


//-------------------------------------------------------------------------------------------------------
//  Create
//-------------------------------------------------------------------------------------------------------
void CarModel::RecreateMaterials()
{
	String strI = toStr(iIndex);
	String sCar = resCar + "/" + sDirname;
	bool ghost = eType == CT_GHOST && pSet->rpl_alpha;  //1 || for ghost test
	
	// --------- Materials  -------------------
	
	// if specialised car material (e.g. car_body_FM) exists, use this one instead of e.g. car_body
	// useful macro for choosing between these 2 variants
	#define chooseMat(s) MaterialManager::getSingleton().resourceExists( "car" + String(s) + "_" + sDirname) ? "car"+String(s) + "_" + sDirname : "car"+String(s)
	
	//  ghost car has no interior, particles, trails and uses same material for all meshes
	if (!ghost)
	{	sMtr[Mtr_CarBody]     = chooseMat("_body");		sMtr[Mtr_CarTireFront]  = chooseMat("tire_front");
		sMtr[Mtr_CarInterior] = chooseMat("_interior");	sMtr[Mtr_CarTireRear]   = chooseMat("tire_rear");
		sMtr[Mtr_CarGlass]    = chooseMat("_glass");
	}else
	for (int i=0; i<NumMaterials; i++)
		sMtr[i] = "car_ghost";  //+s old mtr..

	// copy material to a new material with index
	Ogre::MaterialPtr mat;
	for (int i=0; i<NumMaterials; i++)
	{
		mat = Ogre::MaterialManager::getSingleton().getByName(sMtr[i]);
		if (Ogre::MaterialManager::getSingleton().resourceExists(sMtr[i] + strI))
			Ogre::MaterialManager::getSingleton().remove(sMtr[i] + strI);
		mat->clone(sMtr[i] + strI, false);
		sMtr[i] = sMtr[i] + strI;
		//LogO(" === New car mtr name: " + sMtr[i]);
	}
	
	// iterate through all materials and set body_dyn.png with correct index, add car prefix to other textures
	if (!ghost)
	for (int i=0; i < NumMaterials; i++)
	{
		MaterialPtr mtr = (MaterialPtr)MaterialManager::getSingleton().getByName(sMtr[i]);
		if (!mtr.isNull())
		{	Material::TechniqueIterator techIt = mtr->getTechniqueIterator();
			while (techIt.hasMoreElements())
			{	Technique* tech = techIt.getNext();
				Technique::PassIterator passIt = tech->getPassIterator();
				while (passIt.hasMoreElements())
				{	Pass* pass = passIt.getNext();
					Pass::TextureUnitStateIterator tusIt = pass->getTextureUnitStateIterator();
					while (tusIt.hasMoreElements())
					{
						TextureUnitState* tus = tusIt.getNext();
						String sTex = tus->getTextureName();  //!..
						
						// only 1 tire mesh?
						if ( (i == Mtr_CarTireFront || i == Mtr_CarTireRear) 
							&& FileExists(sCar + "_wheel.mesh") 
							&& (tus->getTextureName() == "wheel_front.png" || tus->getTextureName() == "wheel_rear.png") )
						{
							// set same texture for both
							tus->setTextureName("wheel.png");
						}
						
						if (tus->getTextureName() == "body_dyn.png")
							tus->setTextureName("body_dyn" + strI + ".png");
						else
						if (!(StringUtil::startsWith(tus->getTextureName(), "ReflectionCube") ||
								StringUtil::startsWith(tus->getTextureName(), "body_dyn") ||
								tus->getTextureName() == "ReflectionCube" ||
								StringUtil::startsWith(tus->getName(), "shadowmap") ||
								StringUtil::startsWith(tus->getTextureName(), "flat_n")))
							tus->setTextureName(sDirname + "_" + tus->getTextureName());
		}	}	}	}
		if (pSet->shadow_type == 3)
			pApp->setMtrSplits(mtr->getName());
	}
}

void CarModel::setMtrName(const String& entName, const String& mtrName)
{
	Ogre::Entity* ent; Ogre::ManualObject* manual;

	if (pSceneMgr->hasEntity(entName))
	{
		pSceneMgr->getEntity(entName)->setMaterialName(mtrName);
	}
	else if (pSceneMgr->hasManualObject(entName))
	{
		pSceneMgr->getManualObject(entName)->setMaterialName(0, mtrName);
	}
}


void CarModel::setMtrNames()
{
	if (FileExists(resCar + "/" + sDirname + "_body00_add.png")
	 && FileExists(resCar + "/" + sDirname + "_body00_red.png"))
		setMtrName("Car"+toStr(iIndex), sMtr[Mtr_CarBody]);
	setMtrName("Car.interior"+toStr(iIndex), sMtr[Mtr_CarInterior]);
	setMtrName("Car.glass"+toStr(iIndex), sMtr[Mtr_CarGlass]);
	for (int w=0; w<4; ++w)
		setMtrName("Wheel"+toStr(iIndex)+"_"+toStr(w), w < 2 ? sMtr[Mtr_CarTireFront] : sMtr[Mtr_CarTireRear]);
}

void CarModel::Create(int car)
{
	if (!pCar) return;
	String strI = toStr(iIndex);
	
	bool ghost = eType == CT_GHOST && pSet->rpl_alpha;  //1 || for ghost test
	
	//  Resource locations -----------------------------------------
	/// Add a resource group for this car
	Ogre::ResourceGroupManager::getSingleton().createResourceGroup("Car" + strI);
	Ogre::Root::getSingletonPtr()->addResourceLocation(PATHMANAGER::GetCacheDir(), "FileSystem");
	resCar = PATHMANAGER::GetCarPath() + "/" + sDirname + "/textures";
	Ogre::Root::getSingletonPtr()->addResourceLocation(resCar, "FileSystem", "Car" + strI);
	
	String sCar = resCar + "/" + sDirname;
	
	// Change color here - cache has to be created before loading model
	ChangeClr(car);
	
	pMainNode = pSceneMgr->getRootSceneNode()->createChildSceneNode();

	//  --------  Follow Camera  --------
	if (mCamera)
	{
		fCam = new FollowCamera(mCamera);
		fCam->mGoalNode = pMainNode;
		fCam->loadCameras();
	}
	
	RecreateMaterials();
	
	// reflection
	CreateReflection();

	//  car Models:  body, interior, glass  -------
	//vis flags:  2 not rendered in reflections  16 off by in-car camera
	SceneNode* ncart = pMainNode->createChildSceneNode();
	
	//  body  ----------------------
	Ogre::Vector3 vPofs(0,0,0);
	AxisAlignedBox bodyBox;  Ogre::uint8 g = RQG_CarGhost;

	sCar = resCar + "/" + sDirname;
	if (FileExists(sCar + "_body.mesh"))
	{
		Entity* eCar = pSceneMgr->createEntity("Car"+ strI, sDirname + "_" + "body.mesh", "Car" + strI);

		//eCar->setCastShadows(false);
		bodyBox = eCar->getBoundingBox();
		if (ghost)  {  eCar->setRenderQueueGroup(g);  eCar->setCastShadows(false);  }
		ncart->attachObject(eCar);  eCar->setVisibilityFlags(RV_Car);
	}else{
		ManualObject* mCar = CreateModel(pSceneMgr, sMtr[Mtr_CarBody], &pCar->bodymodel.mesh, vPofs, false, false, "Car"+strI);
		bodyBox = mCar->getBoundingBox();
		if (mCar){	if (ghost)  {  mCar->setRenderQueueGroup(g);  mCar->setCastShadows(false);  }
			ncart->attachObject(mCar);  mCar->setVisibilityFlags(RV_Car);  }
	}

	//  interior  ----------------------
	vPofs = Vector3(pCar->vInteriorOffset[0],pCar->vInteriorOffset[1],pCar->vInteriorOffset[2]);  //x+ back y+ down z+ right

	if (!ghost)
	if (FileExists(sCar + "_interior.mesh"))
	{
		Entity* eInter = pSceneMgr->createEntity("Car.interior"+ strI, sDirname + "_" + "interior.mesh", "Car" + strI);
		//eInter->setCastShadows(false);
		if (ghost)  {  eInter->setRenderQueueGroup(g);  eInter->setCastShadows(false);  }
		ncart->attachObject(eInter);  eInter->setVisibilityFlags(RV_Car);
	}else{
		ManualObject* mInter = CreateModel(pSceneMgr, sMtr[Mtr_CarInterior],&pCar->interiormodel.mesh, vPofs, false, false, "Car.interior"+strI);
		//mInter->setCastShadows(false);
		if (mInter){  if (ghost)  {  mInter->setRenderQueueGroup(g);  mInter->setCastShadows(false);  }
			ncart->attachObject(mInter);  mInter->setVisibilityFlags(RV_Car);  }
	}
	
	//  glass  ----------------------
	vPofs = Vector3(0,0,0);

	if (FileExists(sCar + "_glass.mesh"))
	{
		Entity* eGlass = pSceneMgr->createEntity("Car.glass"+ strI, sDirname + "_" + "glass.mesh", "Car" + strI);
		if (ghost)  {  eGlass->setRenderQueueGroup(g);  eGlass->setCastShadows(false);  }  else
			eGlass->setRenderQueueGroup(RQG_CarGlass);  eGlass->setVisibilityFlags(RV_CarGlass);
		ncart->attachObject(eGlass);
	}else{
		ManualObject* mGlass = CreateModel(pSceneMgr, sMtr[Mtr_CarGlass], &pCar->glassmodel.mesh, vPofs, false, false, "Car.glass"+strI);
		if (mGlass){  mGlass->setRenderQueueGroup(ghost ? g : RQG_CarGlass);  if (ghost)  mGlass->setCastShadows(false);
			ncart->attachObject(mGlass);  mGlass->setVisibilityFlags(RV_CarGlass);  }
	}
	
	///  save .mesh
	/**  MeshPtr mpCar = mCar->convertToMesh("MeshCar");
	Ogre::MeshSerializer* msr = new Ogre::MeshSerializer();
	msr->exportMesh(mpCar.getPointer(), "car.mesh");/**/


	//  wheels  ----------------------
	for (int w=0; w < 4; w++)
	{
		// only 1 mesh for both?
		String siw = "Wheel"+ strI + "_" +toStr(w);
		if (FileExists(sCar + "_wheel.mesh"))
		{
			Entity* eWh = pSceneMgr->createEntity(siw, sDirname + "_wheel.mesh", "Car" + strI);
			if (ghost)  {  eWh->setRenderQueueGroup(g);  eWh->setCastShadows(false);  }
			ndWh[w] = pSceneMgr->getRootSceneNode()->createChildSceneNode();
			ndWh[w]->attachObject(eWh);  eWh->setVisibilityFlags(RV_Car);
		}else{
			if (w < 2 && FileExists(sCar + "_wheel_front.mesh"))
			{
				Entity* eWh = pSceneMgr->createEntity(siw, sDirname + "_" + "wheel_front.mesh", "Car" + strI);
				if (ghost)  {  eWh->setRenderQueueGroup(g);  eWh->setCastShadows(false);  }
				ndWh[w] = pSceneMgr->getRootSceneNode()->createChildSceneNode();
				ndWh[w]->attachObject(eWh);  eWh->setVisibilityFlags(RV_Car);
			}else
			if (FileExists(sCar + "_wheel_rear.mesh"))
			{
				Entity* eWh = pSceneMgr->createEntity(siw, sDirname + "_" + "wheel_rear.mesh", "Car" + strI);
				if (ghost)  {  eWh->setRenderQueueGroup(g);  eWh->setCastShadows(false);  }
				ndWh[w] = pSceneMgr->getRootSceneNode()->createChildSceneNode();
				ndWh[w]->attachObject(eWh);  eWh->setVisibilityFlags(RV_Car);
			}else{
				ManualObject* mWh;
				if (w < 2)	mWh = CreateModel(pSceneMgr, sMtr[Mtr_CarTireFront], &pCar->wheelmodelfront.mesh, vPofs, true, false, siw);
				else		mWh = CreateModel(pSceneMgr, sMtr[Mtr_CarTireRear],  &pCar->wheelmodelrear.mesh, vPofs, true, false, siw);
				if (mWh)  {
				if (ghost)  {  mWh->setRenderQueueGroup(g);  mWh->setCastShadows(false);  }
				ndWh[w] = pSceneMgr->getRootSceneNode()->createChildSceneNode();
				ndWh[w]->attachObject(mWh);  mWh->setVisibilityFlags(RV_Car);  }
			}
		}
	}


	///  world hit sparks  ------------------------
	//if (!ghost)//-
	if (!ph)  {
		ph = pSceneMgr->createParticleSystem("Hit"+strI, "Sparks");
		pSceneMgr->getRootSceneNode()->createChildSceneNode()->attachObject(ph);
		ph->getEmitter(0)->setEmissionRate(0);  }

	///  boost emitters  ------------------------
	for (int i=0; i < 2; i++)
	{
		String si = strI + "_" +toStr(i);
		if (!pb[i])  {
			pb[i] = pSceneMgr->createParticleSystem("Boost"+si, "Boost");
			if (1/*!pCar->manualExhaustPos*/)
			{
				// no exhaust pos in car file, guess from bounding box
				Vector3 bsize = (bodyBox.getMaximum() - bodyBox.getMinimum())*0.5,
					bcenter = bodyBox.getMaximum() + bodyBox.getMinimum();
				LogO("Car body bbox :  size " + toStr(bsize) + ",  center " + toStr(bcenter));
				SceneNode* nb = pMainNode->createChildSceneNode(bcenter+
					Vector3(bsize.x * 0.97, bsize.y * 0.65, bsize.z * 0.65 * (i==0 ? 1 : -1) ));
					//Vector3(1.9 /*back*/, 0.1 /*up*/, 0.6 * (i==0 ? 1 : -1)/*sides*/ ));
				nb->attachObject(pb[i]);
			}else{
				// use exhaust pos values from car file
				Vector3 pos;
				if (i==0)
					pos = Vector3(pCar->exhaustPosition[0], pCar->exhaustPosition[1], pCar->exhaustPosition[2]);
				else if (!pCar->has2exhausts)
					continue;
				else
					pos = Vector3(pCar->exhaustPosition[0], pCar->exhaustPosition[1], -1*pCar->exhaustPosition[2]);

				SceneNode* nb = pMainNode->createChildSceneNode(pos);
				nb->attachObject(pb[i]); 
			}
			pb[i]->getEmitter(0)->setEmissionRate(0);
		}
	}

	///  wheel emitters  ------------------------
	if (!ghost)
	for (int w=0; w < 4; w++)
	{
		String siw = strI + "_" +toStr(w);
		if (!ps[w])  {
			ps[w] = pSceneMgr->createParticleSystem("Smoke"+siw, sc->sParSmoke);
			pSceneMgr->getRootSceneNode()->createChildSceneNode()->attachObject(ps[w]);
			ps[w]->getEmitter(0)->setEmissionRate(0);  }
		if (!pm[w])  {
			pm[w] = pSceneMgr->createParticleSystem("Mud"+siw, sc->sParMud);
			pSceneMgr->getRootSceneNode()->createChildSceneNode()->attachObject(pm[w]);
			pm[w]->getEmitter(0)->setEmissionRate(0);  }
		if (!pd[w])  {
			pd[w] = pSceneMgr->createParticleSystem("Dust"+siw, sc->sParDust);
			pSceneMgr->getRootSceneNode()->createChildSceneNode()->attachObject(pd[w]);
			pd[w]->getEmitter(0)->setEmissionRate(0);  }

		//  trails
		if (!ndWhE[w])
			ndWhE[w] = pSceneMgr->getRootSceneNode()->createChildSceneNode();

		if (!whTrl[w])  {
			NameValuePairList params;
			params["numberOfChains"] = "1";
			params["maxElements"] = toStr(320 * pSet->trails_len);  //80

			whTrl[w] = (RibbonTrail*)pSceneMgr->createMovableObject("RibbonTrail", &params);
			whTrl[w]->setInitialColour(0, 0.1,0.1,0.1, 0);
			pSceneMgr->getRootSceneNode()->attachObject(whTrl[w]);
			whTrl[w]->setMaterialName("TireTrail");
			whTrl[w]->setCastShadows(false);
			whTrl[w]->addNode(ndWhE[w]);
		}
			whTrl[w]->setTrailLength(90 * pSet->trails_len);  //30
			whTrl[w]->setInitialColour(0, 0.1f,0.1f,0.1f, 0);
			whTrl[w]->setColourChange(0, 0.0,0.0,0.0, /*fade*/0.08f * 1.f / pSet->trails_len);
			whTrl[w]->setInitialWidth(0, 0.16f);  //0.18 0.2
	}

	UpdParsTrails();
	
	//  reload car materials
	for (int i = 0; i < NumMaterials; ++i)
		ReloadTex(sMtr[i]);
		
	setMtrNames();
}

//  ----------------- Reflection ------------------------
void CarModel::CreateReflection()
{
	pReflect = new CarReflection(pSet, pSceneMgr, iIndex);
	for (int i=0; i<NumMaterials; i++)
		pReflect->sMtr[i] = sMtr[i];

	pReflect->Create();
}


void CarModel::UpdParsTrails(bool visible)
{
	bool vis = visible && pSet->particles;
	for (int w=0; w < 4; w++)
	{
		Ogre::uint8 grp = RQG_CarTrails;  //9=road  after glass
		if (w < 2 &&
			pb[w])	{	pb[w]->setVisible(vis);  pb[w]->setRenderQueueGroup(grp);  }
		if (whTrl[w]){  whTrl[w]->setVisible(visible && pSet->trails);  whTrl[w]->setRenderQueueGroup(grp);  }  grp = RQG_CarParticles;
		if (ps[w])	{	ps[w]->setVisible(vis);  ps[w]->setRenderQueueGroup(grp);  }  // vdr only && !sc.ter
		if (pm[w])	{	pm[w]->setVisible(vis);  pm[w]->setRenderQueueGroup(grp);  }
		if (pd[w])	{	pd[w]->setVisible(vis);  pd[w]->setRenderQueueGroup(grp);  }
		if (ph)		{	ph->setVisible(vis);     ph->setRenderQueueGroup(grp);     }
	}
}


///  terrain mtr from blend maps
//-------------------------------------------------------------------------------------------------------
void CarModel::UpdWhTerMtr()
{
	if (!pCar || !ndWh[0])  return;
	if (!terrain || !blendMtr)	// vdr trk
	{
		for (int i=0; i<4; ++i)  // for particles/trails only
		{	whTerMtr[i] = pCar->dynamics.bWhOnRoad[i] ? 0 : 1;
			whRoadMtr[i] = pCar->dynamics.bWhOnRoad[i];  }
		return;
	}
	// if whTerMtr == 0 wheel is on road and mtr is in whRoadMtr (now only for road/pipe)
	// TODO: road has only 1 surface, extend to 4, editor tabs, surfaces.txt, alpha transition?...

	int t = blendMapSize;
	Real tws = sc->td.fTerWorldSize;

	//  wheels
	for (int i=0; i<4; ++i)
	{
		Vector3 w = ndWh[i]->getPosition();
		int mx = (w.x + 0.5*tws)/tws*t, my = (w.z + 0.5*tws)/tws*t;
		mx = std::max(0,std::min(t-1, mx)), my = std::max(0,std::min(t-1, my));

		int mtr = pCar->dynamics.bWhOnRoad[i] ? 0 : blendMtr[my*t + mx];
		whTerMtr[i] = mtr;
		whRoadMtr[i] = pCar->dynamics.bWhOnRoad[i];

		///  vdr set surface for wheel
		TRACKSURFACE* tsu = &pGame->track.tracksurfaces[mtr];
		pCar->dynamics.terSurf[i] = tsu;
		pCar->dynamics.bTerrain = true;
	}
}


//-------------------------------------------------------------------------------------------------------
//  utils
//-------------------------------------------------------------------------------------------------------

void CarModel::ChangeClr(int car)
{
	bool add = 1;
	float c_h = pSet->car_hue[car], c_s = pSet->car_sat[car], c_v = pSet->car_val[car];
	color.setHSB(1-c_h,c_s*0.25+0.75,1/*c_v*2+0.7*/);  //set, mini pos clr

	Image ima;	try{
		ima.load(sDirname + "_body00_add.png", "Car" + toStr(iIndex));  // add, not colored
	}catch(...){  add = 0;  }
	uchar* da = 0;  size_t incRow,incRowA=0, inc1=0,inc1A=0;
	if (add)
	{	PixelBox pba = ima.getPixelBox();
		da = (uchar*)pba.data;  incRowA = pba.rowPitch;
		inc1A = PixelUtil::getNumElemBytes(pba.format);
	}
	String svName = PATHMANAGER::GetCacheDir() + "/body_dyn" + toStr(iIndex) + ".png";  // dynamic
	Image im;  try{
		im.load(sDirname + "_body00_red.png", "Car" + toStr(iIndex));  // original red diffuse
	}catch(...){  return;  }
	if (im.getWidth())
	{
		PixelBox pb = im.getPixelBox();
		size_t xw = pb.getWidth(), yw = pb.getHeight();

		uchar* d = (uchar*)pb.data;  incRow = pb.rowPitch;
		inc1 = PixelUtil::getNumElemBytes(pb.format);

		//Ogre::LogManager::getSingleton().logMessage(
			//"img clr +++  w "+toStr(xw)+"  h "+toStr(yw)+"  pf "+toStr(pb.format)+"  iA "+toStr(inc1A));

		size_t x,y,a,aa;
		for (y = 0; y < yw; ++y)
		{	a = y*incRow*inc1, aa = y*incRowA*inc1A;
		for (x = 0; x < xw; ++x)
		{
			uchar r,g,b;
			if (da && da[aa+3] > 60)  // adding area (not transparent)
			{	r = da[aa];  g = da[aa+1];  b = da[aa+2];	}
			else
			{	r = d[a], g = d[a+1], b = d[a+2];  // get
				ColourValue c(r/255.f,g/255.f,b/255.f);  //

				Real h,s,v;  // hue shift
				c.getHSB(&h,&s,&v);
				h += c_h;  if (h>1.f) h-=1.f;  // 0..1
				s += c_s;  v += c_v;  // -1..1
				c.setHSB(h,s,v);

				r = c.r*255;  g = c.g*255;  b = c.b*255;  // set
			}
			d[a] = r;  d[a+1] = g;  d[a+2] = b;	 // write back
			a += inc1;  aa += inc1A;  // next pixel
		}	}
	}
	im.save(svName);
	ReloadTex(sMtr[Mtr_CarBody]);
}


ManualObject* CarModel::CreateModel(SceneManager* sceneMgr, const String& mat, class VERTEXARRAY* a, Vector3 vPofs, bool flip, bool track, const String& name)
{
	int verts = a->vertices.size();
	if (verts == 0)  return NULL;
	int tcs   = a->texcoords[0].size(); //-
	int norms = a->normals.size();
	int faces = a->faces.size();
	// norms = verts, verts % 3 == 0

	ManualObject* m;
	if (name == "")
		m = sceneMgr->createManualObject();
	else
		m = sceneMgr->createManualObject(name);
	m->begin(mat, RenderOperation::OT_TRIANGLE_LIST);

	int t = 0;
	if (track)
	{	for (int v = 0; v < verts; v += 3)
		{
			m->position(a->vertices[v+0], a->vertices[v+2], -a->vertices[v+1]);
			if (norms)
			m->normal(	a->normals [v+0], a->normals [v+2], -a->normals [v+1]);
			if (t < tcs)
			{	m->textureCoord(a->texcoords[0][t], a->texcoords[0][t+1]);  t += 2;	}
		}
		for (int f = 0; f < faces; ++f)
			m->index(a->faces[f]);
	}else
	if (flip)
	{	for (int v = 0; v < verts; v += 3)
		{
			m->position(a->vertices[v], a->vertices[v+1], a->vertices[v+2]);
			if (norms)
			m->normal(  a->normals [v], a->normals [v+1], a->normals [v+2]);
			if (t < tcs)
			{	m->textureCoord(a->texcoords[0][t], a->texcoords[0][t+1]);  t += 2;	}
		}
		for (int f = 0; f < faces; f += 3)
		{	m->index(a->faces[f+2]);  m->index(a->faces[f+1]);  m->index(a->faces[f]);	}
	}else
	{	for (int v = 0; v < verts; v += 3)
		{
			m->position(-a->vertices[v+1]+vPofs.x, -a->vertices[v+2]+vPofs.y, a->vertices[v]+vPofs.z);
			if (norms)
			m->normal(	-a->normals [v+1], -a->normals [v+2], a->normals [v]);
			if (t < tcs)
			{	m->textureCoord(a->texcoords[0][t], a->texcoords[0][t+1]);  t += 2;	}
		}
		for (int f = 0; f < faces; f += 3)
		{	m->index(a->faces[f+2]);  m->index(a->faces[f+1]);  m->index(a->faces[f]);	}
	}
	m->end();
	return m;
}


void CarModel::ReloadTex(String mtrName)
{
	MaterialPtr mtr = (MaterialPtr)MaterialManager::getSingleton().getByName(mtrName);
	if (!mtr.isNull())
	{	Material::TechniqueIterator techIt = mtr->getTechniqueIterator();
		while (techIt.hasMoreElements())
		{	Technique* tech = techIt.getNext();
			Technique::PassIterator passIt = tech->getPassIterator();
			while (passIt.hasMoreElements())
			{	Pass* pass = passIt.getNext();
				Pass::TextureUnitStateIterator tusIt = pass->getTextureUnitStateIterator();
				while (tusIt.hasMoreElements())
				{	TextureUnitState* tus = tusIt.getNext();  String name = tus->getTextureName();
					if (! (Ogre::StringUtil::startsWith(name, "ReflectionCube", false) || name == "ReflectionCube") )
					{
						Ogre::LogManager::getSingletonPtr()->logMessage( "Tex Reload: " + name );
						TexturePtr tex = (TexturePtr)Ogre::TextureManager::getSingleton().getByName( name );
						if (!tex.isNull())
						{							
							tex->reload();
						}
					}
				}
	}	}	}	
}

