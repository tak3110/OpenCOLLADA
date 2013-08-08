/*
    Copyright (c) 2008-2009 NetAllied Systems GmbH

	This file is part of COLLADAMaya.

    Portions of the code are:
    Copyright (c) 2005-2007 Feeling Software Inc.
    Copyright (c) 2005-2007 Sony Computer Entertainment America
    Copyright (c) 2004-2005 Alias Systems Corp.

    Licensed under the MIT Open Source License,
    for details please see LICENSE file or the website
    http://www.opensource.org/licenses/mit-license.php
*/

#include "COLLADAMayaStableHeaders.h"
#include "COLLADAMayaDocumentExporter.h"
#include "COLLADAMayaSceneGraph.h"
#include "COLLADAMayaGeometryExporter.h"
#include "COLLADAMayaVisualSceneExporter.h"
#include "COLLADAMayaEffectExporter.h"
#include "COLLADAMayaImageExporter.h"
#include "COLLADAMayaMaterialExporter.h"
#include "COLLADAMayaAnimationExporter.h"
#include "COLLADAMayaAnimationClipExporter.h"
#include "COLLADAMayaAnimationSampleCache.h"
#include "COLLADAMayaControllerExporter.h"
#include "COLLADAMayaLightExporter.h"
#include "COLLADAMayaCameraExporter.h"
#include "COLLADAMayaDagHelper.h"
#include "COLLADAMayaShaderHelper.h"
#include "COLLADAMayaConversion.h"
#include "COLLADAMayaExportOptions.h"
#include "COLLADAMayaSyntax.h"
#include "COLLADAMayaReferenceManager.h"

#include "COLLADASWAsset.h"
#include "COLLADASWScene.h"
#include "COLLADASWConstants.h"

#include <maya/MFileIO.h>
#include <maya/MFnAttribute.h>

#ifndef AD_IGNORE_MODIFY
//AD_EXPORT_MATERIAL_EXT_ATTR
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#define DAEFC_DYNAMIC_ATTRIBUTES_ELEMENT "dynamic_attributes"
#endif//AD_IGNORE_MODIFY


namespace COLLADAMaya
{

#ifndef AD_IGNORE_MODIFY
//AD_EXPORT_MATERIAL_EXT_ATTR
    static String s_strDynAttrName;
#endif//AD_IGNORE_MODIFY
    
    //---------------------------------------------------------------
    DocumentExporter::DocumentExporter ( const NativeString& fileName )
        : mStreamWriter ( fileName, ExportOptions::doublePrecision () )
            , mFileName ( fileName )
            , mSceneGraph ( NULL )
            , mIsImport ( true )
            , mAnimationCache ( NULL )
            , mMaterialExporter ( NULL )
            , mEffectExporter ( NULL )
            , mImageExporter ( NULL )
            , mGeometryExporter ( NULL )
            , mVisualSceneExporter ( NULL )
            , mAnimationExporter ( NULL )
            , mAnimationClipExporter ( NULL )
            , mControllerExporter ( NULL )
            , mLightExporter ( NULL )
            , mCameraExporter ( NULL )
            , mSceneId ( "MayaScene" )
            , mDigitTolerance (FLOAT_TOLERANCE)
    {
        if ( ExportOptions::doublePrecision () )
        {
            mDigitTolerance = DOUBLE_TOLERANCE;
        }
#ifndef AD_IGNORE_MODIFY
//AD_EXPORT_MATERIAL_EXT_ATTR
        s_strDynAttrName = DAEFC_DYNAMIC_ATTRIBUTES_ELEMENT;
#endif//AD_IGNORE_MODIFY
    }

    //---------------------------------------------------------------
    DocumentExporter::~DocumentExporter()
    {
        releaseLibraries(); // The libraries should already have been released
    }

    //---------------------------------------------------------------
    // Create the parsing libraries: we want access to the libraries only during import/export time.
    void DocumentExporter::createLibraries()
    {
        releaseLibraries();
        
        // Get the sceneID (assign a name to the scene)
        MString sceneName;
        MGlobal::executeCommand ( MString ( "file -q -ns" ), sceneName );
        if ( sceneName.length() != 0 ) mSceneId = sceneName.asChar();

        // Initialize the reference manager
        ReferenceManager::getInstance()->initialize ();

        // Create the cache for the animations
        mAnimationCache = new AnimationSampleCache();

        // Create the basic elements
        mSceneGraph = new SceneGraph ( this );
        mMaterialExporter = new MaterialExporter ( &mStreamWriter, this );
        mEffectExporter = new EffectExporter ( &mStreamWriter, this );
        mImageExporter = new ImageExporter ( &mStreamWriter );
        mGeometryExporter = new GeometryExporter ( &mStreamWriter, this );
        mVisualSceneExporter = new VisualSceneExporter ( &mStreamWriter, this, mSceneId );
        mAnimationExporter = new AnimationExporter ( &mStreamWriter, this );
        mAnimationClipExporter = new AnimationClipExporter ( &mStreamWriter );
        mControllerExporter = new ControllerExporter ( &mStreamWriter, this );
        mLightExporter = new LightExporter ( &mStreamWriter, this );
        mCameraExporter = new CameraExporter ( &mStreamWriter, this );
    }

    //---------------------------------------------------------------
    void DocumentExporter::releaseLibraries()
    {
        delete mAnimationCache;
        delete mSceneGraph;
        delete mMaterialExporter;
        delete mEffectExporter;
        delete mImageExporter;
        delete mGeometryExporter;
        delete mVisualSceneExporter;
        delete mAnimationExporter;
        delete mAnimationClipExporter;
        delete mControllerExporter;
        delete mLightExporter;
        delete mCameraExporter;
    }


    //---------------------------------------------------------------
    void DocumentExporter::exportCurrentScene ( bool selectionOnly )
    {
        mIsImport = false;

        // Create the import/export library helpers.
        createLibraries();

        mStreamWriter.startDocument();

        // Build the scene graph
        if ( mSceneGraph->create ( selectionOnly ) )
        {
            // Export the asset of the document.
            exportAsset();

            if ( !ExportOptions::exportMaterialsOnly () )
            {
                // Start by caching the expressions that will be sampled
                mSceneGraph->sampleAnimationExpressions();

                // Export the lights.
                mLightExporter->exportLights();

                // Export the cameras.
                mCameraExporter->exportCameras();

                // Export the material URLs and get the material list
                MaterialMap* materialMap = mMaterialExporter->exportMaterials();

                // Export the effects (materials)
                const ImageMap* imageMap = mEffectExporter->exportEffects ( materialMap );

                // Export the images
                mImageExporter->exportImages ( imageMap );

                // Export the controllers. Must be done before the geometries, to decide, which
                // geometries have to be exported (for example, if the controller need an invisible 
                // geometry, we also have to export it).
                mControllerExporter->exportControllers();

                // Export the geometries
                mGeometryExporter->exportGeometries();

                // Export the visual scene
                bool visualSceneExported = mVisualSceneExporter->exportVisualScenes();

                // Export the animations
                const AnimationClipList* animationClips = mAnimationExporter->exportAnimations();

                // Export the animation clips
                mAnimationClipExporter->exportAnimationClips ( animationClips );

                // Export the scene
                if ( visualSceneExported ) exportScene();
            }
            else
            {
                // Export the material URLs and get the material list
                MaterialMap* materialMap = mMaterialExporter->exportMaterials();

                // Export the effects (materials)
                const ImageMap* imageMap = mEffectExporter->exportEffects ( materialMap );

                // Export the images
                mImageExporter->exportImages ( imageMap );
            }
        }

        mStreamWriter.endDocument();
    }

    //---------------------------------------------------------------
    void DocumentExporter::exportAsset()
    {
#ifndef AD_IGNORE_MODIFY
//AD_OMIT_EXPORT_INFO
        if ( !ExportOptions::omitCusomVersion() )
        {
            COLLADASW::Extra extra( &mStreamWriter );
            extra.openExtra();

            COLLADASW::Technique techniqueSource ( &mStreamWriter );
            techniqueSource.openTechnique( PROFILE_MAYA );
            techniqueSource.addParameter( PARAMETER_TRANSLATOR_VENDOR_CUSTOM, String(TRANSLATOR_VENDOR_CUSTOM) );    //TODO const char* ‘Î‰žŠÖ”‚ð—pˆÓ‚·‚é...
            techniqueSource.addParameter( PARAMETER_TRANSLATOR_VERSION_CUSTOM, String(TRANSLATOR_VERSION_CUSTOM) );
            techniqueSource.closeTechnique();
            extra.closeExtra();
        }
#endif//AD_IGNORE_MODIFY
        COLLADASW::Asset asset ( &mStreamWriter );

#ifndef AD_IGNORE_MODIFY
//AD_OMIT_EXPORT_INFO
        char* userName = NULL;
        if( !ExportOptions::omitAuthor() )
        {
            userName = getenv ( USERNAME );
            if ( !userName || *userName == 0 ) 
            {
                userName = getenv ( USER );
            }
        }

        if ( userName && *userName != 0 ) 
        {
            asset.getContributor().mAuthor = NativeString( userName ).toUtf8String();
        }
#else//AD_IGNORE_MODIFY
        // Add contributor information
        // Set the author
        char* userName = getenv ( USERNAME );

        if ( !userName || *userName == 0 ) 
		{
			userName = getenv ( USER );
		}
        if ( userName && *userName != 0 ) 
		{
			asset.getContributor().mAuthor = NativeString( userName ).toUtf8String();
		}
#endif//AD_IGNORE_MODIFY

        // Source is the scene we have exported from
        String currentScene = MFileIO::currentFile().asChar();
        if ( currentScene.size() > 0 )
        {
            COLLADASW::URI sourceFileUri ( COLLADASW::URI::nativePathToUri ( currentScene ) );
            if ( sourceFileUri.getScheme ().empty () )
                sourceFileUri.setScheme ( COLLADASW::URI::SCHEME_FILE );
            asset.getContributor().mSourceData = sourceFileUri.getURIString();
        }

#ifndef AD_IGNORE_MODIFY
//AD_OMIT_EXPORT_INFO
        if ( !ExportOptions::omitAuthoringTool() )
        {
            asset.getContributor().mAuthoringTool = AUTHORING_TOOL_NAME + MGlobal::mayaVersion().asChar();
        }
#else//AD_IGNORE_MODIFY
        asset.getContributor().mAuthoringTool = AUTHORING_TOOL_NAME + MGlobal::mayaVersion().asChar();
#endif//AD_IGNORE_MODIFY

        // comments
        MString optstr = MString ( "\n\t\t\tColladaMaya export options: " )
            + "\n\t\t\tbakeTransforms=" + ExportOptions::bakeTransforms() 
            + ";relativePaths=" + ExportOptions::relativePaths() 
            + ";copyTextures=" + ExportOptions::copyTextures() 
            + ";exportTriangles=" + ExportOptions::exportTriangles() 
            + ";exportCgfxFileReferences=" + ExportOptions::exportCgfxFileReferences() 
            + ";\n\t\t\tisSampling=" + ExportOptions::isSampling() 
            + ";curveConstrainSampling=" + ExportOptions::curveConstrainSampling()
            + ";removeStaticCurves=" + ExportOptions::removeStaticCurves() 
            + ";exportPolygonMeshes=" + ExportOptions::exportPolygonMeshes() 
            + ";exportLights=" + ExportOptions::exportLights() 
            + ";\n\t\t\texportCameras=" + ExportOptions::exportCameras() 
            + ";exportJointsAndSkin=" + ExportOptions::exportJointsAndSkin() 
            + ";exportAnimations=" + ExportOptions::exportAnimations()
            + ";exportInvisibleNodes=" + ExportOptions::exportInvisibleNodes()
            + ";exportDefaultCameras=" + ExportOptions::exportDefaultCameras()
            + ";\n\t\t\texportTexCoords=" + ExportOptions::exportTexCoords()
            + ";exportNormals=" + ExportOptions::exportNormals() 
            + ";exportNormalsPerVertex=" + ExportOptions::exportNormalsPerVertex() 
            + ";exportVertexColors=" + ExportOptions::exportVertexColors()
            + ";exportVertexColorsPerVertex=" + ExportOptions::exportVertexColorsPerVertex()
            + ";\n\t\t\texportTexTangents=" + ExportOptions::exportTexTangents() 
            + ";exportTangents=" + ExportOptions::exportTangents() 
            + ";exportReferencedMaterials=" + ExportOptions::exportReferencedMaterials() 
            + ";exportMaterialsOnly=" + ExportOptions::exportMaterialsOnly() 
            + ";\n\t\t\texportXRefs=" + ExportOptions::exportXRefs() 
            + ";dereferenceXRefs=" + ExportOptions::dereferenceXRefs() 
            + ";exportCameraAsLookat=" + ExportOptions::exportCameraAsLookat() 
            + ";cameraXFov=" + ExportOptions::cameraXFov() 
            + ";cameraYFov=" + ExportOptions::cameraYFov() 
            + ";doublePrecision=" + ExportOptions::doublePrecision () + "\n\t\t";
        asset.getContributor().mComments = optstr.asChar();

        // Up axis
        if ( MGlobal::isYAxisUp() ) 
            asset.setUpAxisType ( COLLADASW::Asset::Y_UP );
        else if ( MGlobal::isZAxisUp() ) 
            asset.setUpAxisType ( COLLADASW::Asset::Z_UP );

        // Retrieve the linear unit name
        MString mayaLinearUnitName;
        MGlobal::executeCommand ( "currentUnit -q -linear -fullName;", mayaLinearUnitName );
        String linearUnitName = mayaLinearUnitName.asChar();

        // Get the UI unit type (internal units are centimeter, collada want
        // a number relative to meters).
        // All transform components with units will be in maya's internal units
        // (radians for rotations and centimeters for translations).
        MDistance testDistance ( 1.0f, MDistance::uiUnit() );

        // Get the conversion factor relative to meters for the collada document.
        // For example, 1.0 for the name "meter"; 1000 for the name "kilometer";
        // 0.3048 for the name "foot".
        double colladaConversionFactor = ( float ) testDistance.as ( MDistance::kMeters );
        float colladaUnitFactor = float ( 1.0 / colladaConversionFactor );
        asset.setUnit ( linearUnitName, colladaConversionFactor );
#ifndef AD_IGNORE_MODIFY
//AD_OMIT_EXPORT_INFO
        {
            time_t _t;

            if( ExportOptions::omitTimestamps() ){
                _t = 0;
            }else{
                time ( &_t );
            }
            asset.setCreatedTime( _t );
        }
#endif//AD_IGNORE_MODIFY

        // Asset heraus schreiben
        asset.add();
    }

    //---------------------------------------------------------------
    void DocumentExporter::exportScene()
    {
        COLLADASW::Scene scene ( &mStreamWriter, COLLADASW::URI ( EMPTY_STRING, VISUAL_SCENE_NODE_ID ) );
        scene.add();
    }

    //---------------------------------------------------------------
    void DocumentExporter::endExport()
    {
        // Write out the scene parameters
        //  CDOC->SetStartTime((float) AnimationHelper::AnimationStartTime().as(MTime::kSeconds));
        //  CDOC->SetEndTime((float) AnimationHelper::AnimationEndTime().as(MTime::kSeconds));
    }

    //---------------------------
    String DocumentExporter::mayaNameToColladaName ( const MString& str, bool removeNamespace )
    {
        // Replace characters that are supported in Maya,
        // but not supported in collada names
        if ( str == 0 ) return EMPTY_STRING;

        // NathanM: Strip off namespace prefixes
        // TODO: Should really be exposed as an option in the Exporter
        MString mayaName;
        if ( removeNamespace )
        {
            int prefixIndex = str.rindex ( ':' );
            mayaName = ( prefixIndex < 0 ) ? str : str.substring ( prefixIndex + 1, str.length() );
        }
        else mayaName = str;

        const char* c = mayaName.asChar();
        uint length = mayaName.length();
        char* buffer = new char[length+1];

        // Replace offending characters by some that are supported within xml:
        // ':', '|' are replaced by '_'.
        // Ideally, these should be encoded as '%3A' for ':', etc. and decoded at import time.
        //
        for ( uint i = 0; i < length; ++i )
        {
            char d = c[i];

            if ( d == '|' || d == ':' || d == '/' || d == '\\' || d == '(' || d == ')' || d == '[' || d == ']' )
                d = '_';

            buffer[i] = d;
        }
        buffer[length] = '\0';

        MString mayaReturnString ( buffer );
        delete buffer;

        return COLLADABU::Utils::checkNCName( mayaReturnString.asChar() );
    }

    //---------------------------
    String DocumentExporter::dagPathToColladaId ( const MDagPath& dagPath )
    {
        // Make an unique COLLADA Id from a dagPath.
        // We are free to use anything we want for Ids. For now use
        // a honking unique name for readability - but in future we
        // could just use an incrementing integer
        return mayaNameToColladaName ( dagPath.partialPathName(), false );
    }

    //---------------------------
    String DocumentExporter::dagPathToColladaName ( const MDagPath& dagPath )
    {
        // Get a COLLADA suitable node name from a DAG path
        // For import/export symmetry, this should be exactly the
        // Maya node name. If we include any more of the path, we'll
        // get viral node names after repeated import/export.
        MFnDependencyNode node ( dagPath.node() );
        return mayaNameToColladaName ( node.name(), true );
    }

    //---------------------------
    SceneGraph* DocumentExporter::getSceneGraph()
    {
        return mSceneGraph;
    }

    //---------------------------
    const SceneGraph* DocumentExporter::getSceneGraph() const
    {
        return mSceneGraph;
    }

    //---------------------------
    const String& DocumentExporter::getFilename() const
    {
        return mFileName;
    }

    //---------------------------
    COLLADASW::StreamWriter* DocumentExporter::getStreamWriter()
    {
        return &mStreamWriter;
    }

    //---------------------------
    AnimationSampleCache* DocumentExporter::getAnimationCache()
    {
        return mAnimationCache;
    }

    //---------------------------
    MaterialExporter* DocumentExporter::getMaterialExporter()
    {
        return mMaterialExporter;
    }

    //---------------------------
    EffectExporter* DocumentExporter::getEffectExporter()
    {
        return mEffectExporter;
    }

    //---------------------------
    ImageExporter* DocumentExporter::getImageExporter()
    {
        return mImageExporter;
    }

    //---------------------------
    GeometryExporter* DocumentExporter::getGeometryExporter()
    {
        return mGeometryExporter;
    }

    //---------------------------
    VisualSceneExporter* DocumentExporter::getVisualSceneExporter()
    {
        return mVisualSceneExporter;
    }

    //---------------------------
    AnimationExporter* DocumentExporter::getAnimationExporter()
    {
        return mAnimationExporter;
    }

    //---------------------------
    AnimationClipExporter* DocumentExporter::getAnimationClipExporter()
    {
        return mAnimationClipExporter;
    }

    //---------------------------
    ControllerExporter* DocumentExporter::getControllerExporter()
    {
        return mControllerExporter;
    }

    //---------------------------
    LightExporter* DocumentExporter::getLightExporter()
    {
        return mLightExporter;
    }

    //---------------------------
    CameraExporter* DocumentExporter::getCameraExporter()
    {
        return mCameraExporter;
    }

    //---------------------------
    const String& DocumentExporter::getSceneID()
    {
        return mSceneId;
    }

    //---------------------------
    const bool DocumentExporter::getExportSelectedOnly () const
    {
        return getSceneGraph ()->getExportSelectedOnly ();
    }

#ifndef AD_IGNORE_MODIFY
//AD_EXPORT_MATERIAL_EXT_ATTR

    // --------------------------------------
    void DocumentExporter::exportExtraData ( 
        const MObject& node, 
        COLLADASW::BaseExtraTechnique* baseExtraTechnique /*= 0*/ )
    {
        MStatus status;
        MFnDependencyNode dependencyNode ( node, &status );
        if ( status != MStatus::kSuccess )
        {
            return;
        }
        
        uint uiAttributeCount = dependencyNode.attributeCount();
        
        // Flag, if the extra source was opened.
         bool closeExtraSource = false;
        COLLADASW::Extra extraSource ( &mStreamWriter );

        // Create the extra attribute.
        COLLADASW::Technique techniqueSource ( &mStreamWriter );
        
        for ( uint i=0; i< uiAttributeCount; ++i )
        {
            MObject attr = dependencyNode.attribute( i );
            MFnAttribute attrFn(attr);
            
            // Skip some known dynamic attributes
            if (attrFn.name() == "collada") continue; // Used by the DaeDocNode to link entities with the FCollada objects
            if (attrFn.parent() != MObject::kNullObj) continue;
            if (!attrFn.isDynamic() || !attrFn.isStorable() || !attrFn.isReadable() || !attrFn.isWritable()) continue;

            // Well-known dynamic attributes that we don't want to export.
            if (node.hasFn(MFn::kTransform))
            {
                if (attrFn.name() == "lockInfluenceWeights") continue; // used in all joints.
            }

            // Retrieve this attribute's value
            bool wantNetworkedPlug = true;
            MPlug plug = dependencyNode.findPlug(attrFn.object(), wantNetworkedPlug, &status);
            MString name = plug.name();
            if ( status != MStatus::kSuccess )
            {
                continue;
            }
            
            // Get the collada attribute.
            String pathAttributeName = attrFn.name().asChar ();

            if ( exportExtraData ( plug, pathAttributeName, &techniqueSource, extraSource, closeExtraSource, baseExtraTechnique ) )
                closeExtraSource = true;
        }

         // Close the extra source tag, if it is open.
        if ( closeExtraSource )
        {
            if( baseExtraTechnique == NULL )
            {
                techniqueSource.closeChildElement( s_strDynAttrName );
                extraSource.closeExtra();
                techniqueSource.closeTechnique();
            }
        }
     }
 
     // --------------------------------------
    bool DocumentExporter::exportExtraData ( 
        const MPlug& plug, 
        const String& parentAttributeName, 
        COLLADASW::Technique* techniqueSource, 
        COLLADASW::Extra& extraSource, 
        bool openedExtraSource,
        COLLADASW::BaseExtraTechnique* baseExtraTechnique /*= 0*/ )
    {
        MStatus status;

        // Get the profile name of the child attribute.
        //size_t numExtraChildren = plug.numChildren ();
        //for ( uint i=0; i<numExtraChildren; ++i )
        {
            // Get the extra plug.
            //MPlug extraPlug = plug.child ( i, &status );
            MPlug extraPlug = plug;
            if ( status != MStatus::kSuccess ) return false;
            MFnAttribute extraAttribute ( extraPlug.attribute() );
 
            // Check if we have an extra tag of an physical index element (primitive element, 
            // surface, sampler2d or texture). In this case, the current attribute is a compound
            // attribute and holds for every index a child element.
            if ( extraPlug.isCompound () )
            {
#if 1            
                MObject attributeNode(extraPlug.attribute());
                MStatus status;
                MFnDependencyNode dependencyNode ( attributeNode, &status );
                if ( status != MStatus::kSuccess )
                {
                    return openedExtraSource;
                }
                uint uiAttributeCount = dependencyNode.attributeCount();
                    for ( uint i=0; i< uiAttributeCount; ++i )
                {
                    MObject childAttribute = dependencyNode.attribute( i );
                    MFnAttribute childAttrFn(childAttribute);
                
                    bool wantNetworkedPlug = true;
                    MPlug childPlug = dependencyNode.findPlug(childAttrFn.object(), wantNetworkedPlug, &status);
                    
                    String childAttributeName = childAttrFn.name().asChar ();
                    
                    if ( exportExtraData ( childPlug, childAttributeName, techniqueSource, extraSource, openedExtraSource, baseExtraTechnique ) )
                    {
                        openedExtraSource = true;
                    }
                }
                return openedExtraSource;
#else
                // TODO Recursive call for the child plugs.
                MString name = extraAttribute.name ();
                if( name.length() == 0 ) return false;
                String attributeName = name.asUTF8 ();
                if ( exportExtraData ( extraPlug, attributeName, techniqueSource, extraSource, openedExtraSource, baseExtraTechnique ) )
                    openedExtraSource = true;
                return openedExtraSource;
#endif
            }
 
            MString name = extraAttribute.name ();
            if( name.length() == 0 ) return false;
            
            // The attribute with the collada profile name.
            // The profile name has always the path attribute name in front.
            //String profileName = name.asUTF8 ();
            //profileName = profileName.substr ( parentAttributeName.length () + 1 );
            String profileName = PROFILE_MAYA;
            
            String strParamName = name.asUTF8 ();
            
            // Open the extra source tag, if neccessary.
            if ( !openedExtraSource ) 
            {
                if( baseExtraTechnique == NULL )
                {
                    extraSource.openExtra();
                    techniqueSource->openTechnique ( profileName );
                    techniqueSource->addChildElement( s_strDynAttrName );
                }
                else
                {
                    //baseExtraTechnique->addExtraTechniqueChildParameter ( profileName, s_strDynAttrName );
                }
            }
            
             // The attribute value.
            
            String source;
            MObject attribute(extraPlug.attribute());
            if (attribute.hasFn(MFn::kTypedAttribute))
            {
                const char* typeName = NULL;
                MFnTypedAttribute typedAttrFn(extraAttribute.object());
                switch (typedAttrFn.attrType())
                {
                case MFnData::kString: { // Not animatable
                    MString value; extraPlug.getValue(value);
                    if( baseExtraTechnique == NULL )
                    {
                        const String strValue = value.asUTF8();
                        techniqueSource->addParameter( strParamName, strValue );
                    }
                    else
                    {
                        const String strValue = value.asUTF8();
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, strValue );
                    }
                    break; }

                case MFnData::kMatrix: { // Not animatable
                    MMatrix value; DagHelper::getPlugValue(extraPlug, value);
                    double dest[4][4];
                    value.get( dest );
                    if( baseExtraTechnique == NULL )
                    {
                        techniqueSource->addMatrixParameter( strParamName, dest );
                    }
                    else
                    {
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, dest );
                    }
                    break; }

                default: break; // none of the other types will be importable.
                }
            }
            else if (attribute.hasFn(MFn::kNumericAttribute))
            {
                MFnNumericAttribute numericAttrFn(extraAttribute.object());
                switch (numericAttrFn.unitType())
                {
                case MFnNumericData::kBoolean: {
                    bool value; DagHelper::getPlugValue(extraPlug, value);
                    if( baseExtraTechnique == NULL )
                    {
                        techniqueSource->addParameter( strParamName, value );
                    }
                    else
                    {
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, value );
                    }
                    break; }

                case MFnNumericData::kByte:
                case MFnNumericData::kChar:
                case MFnNumericData::kShort:
                case MFnNumericData::k2Short:
                case MFnNumericData::k3Short:
                case MFnNumericData::kLong:
                case MFnNumericData::k2Long:
                case MFnNumericData::k3Long: {
                    int value; DagHelper::getPlugValue(extraPlug, value);
                    if( baseExtraTechnique == NULL )
                    {
                        techniqueSource->addParameter( strParamName, value );
                    }
                    else
                    {
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, value );
                    }
                    break; }

                case MFnNumericData::kDouble:
                case MFnNumericData::kFloat: 
                case MFnNumericData::k2Double:
                case MFnNumericData::k2Float: {
                    float value; DagHelper::getPlugValue(extraPlug, value);
                    if( baseExtraTechnique == NULL )
                    {
                        techniqueSource->addParameter( strParamName, value );
                    }
                    else
                    {
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, value );
                    }
                    break; }

                case MFnNumericData::k3Double:
                case MFnNumericData::k3Float: {
                    int value; DagHelper::getPlugValue(extraPlug, value);
                    if( baseExtraTechnique == NULL )
                    {
                        techniqueSource->addParameter( strParamName, value );
                    }
                    else
                    {
                        baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, value );
                    }
                    break; }
                }
            }
            else if (attribute.hasFn(MFn::kEnumAttribute))
            {
                MFnEnumAttribute enumFn(extraAttribute.object());
                int index; DagHelper::getPlugValue(extraPlug, index);

                MStatus status;
                MString value = enumFn.fieldName((short) index, &status);
                //FUAssert(status, return);
                const String strValue = value.asUTF8();
                if( baseExtraTechnique == NULL )
                {
                    techniqueSource->addParameter( strParamName, strValue );
                }
                else
                {
                    baseExtraTechnique->addExtraTechniqueChildParameter( profileName, s_strDynAttrName, strParamName, strValue );
                }
            }
         }
         return true;
     }
     
#else//AD_IGNORE_MODIFY

//     // --------------------------------------
//     void DocumentExporter::exportExtraData ( 
//         const MObject& node, 
//         const char* key, 
//         const char* secondKey /*= 0*/, 
//         COLLADASW::BaseExtraTechnique* baseExtraTechnique /*= 0*/ )
//     {
//         MStatus status;
//         MFnDependencyNode dependencyNode ( node, &status );
//         if ( status != MStatus::kSuccess ) return;
// 
//         MPlug colladaPlug = dependencyNode.findPlug ( COLLADAFW::ExtraKeys::BASEKEY, &status );
//         if ( status == MStatus::kSuccess ) 
//         {
//             if ( !colladaPlug.isCompound () ) 
//             {
//                 std::cerr << "There exist another attribute with the name \"COLLADA\". No extra tag preservation possible!" << endl;
//                 return;
//             }
// 
//             // Flag, if the extra source was opened.
//             bool closeExtraSource = false;
//             COLLADASW::Extra extraSource ( &mStreamWriter );
// 
//             size_t numChildren = colladaPlug.numChildren ();
//             for ( uint i=0; i<numChildren; ++i )
//             {
//                 MPlug childPlug = colladaPlug.child ( i, &status );
//                 if ( status != MStatus::kSuccess ) continue;
// 
//                 // Get the collada attribute.
//                 MFnAttribute pathAttribute ( childPlug.attribute() );
//                 String pathAttributeName = pathAttribute.name().asChar ();
//                 if ( !COLLADABU::Utils::equals ( pathAttributeName, key ) ) continue;
// 
//                 if ( exportExtraData ( childPlug, pathAttributeName, baseExtraTechnique, extraSource, secondKey ) )
//                     closeExtraSource = true;
//             }
// 
//             // Close the extra source tag, if it is open.
//             if ( closeExtraSource ) 
//                 extraSource.closeExtra();
//         }
//     }
// 
//     // --------------------------------------
//     bool DocumentExporter::exportExtraData ( 
//         const MPlug& childPlug, 
//         const String& parentAttributeName, 
//         COLLADASW::BaseExtraTechnique* baseExtraTechnique, 
//         COLLADASW::Extra& extraSource, 
//         const char* secondKey /*= 0*/ )
//     {
//         MStatus status;
//         bool closeExtraSource = false; 
// 
//         // Get the profile name of the child attribute.
//         size_t numExtraChildren = childPlug.numChildren ();
//         for ( uint i=0; i<numExtraChildren; ++i )
//         {
//             // Get the extra plug.
//             MPlug extraPlug = childPlug.child ( i, &status );
//             if ( status != MStatus::kSuccess ) continue;
//             MFnAttribute extraAttribute ( extraPlug.attribute() );
// 
//             // Check if we have an extra tag of an physical index element (primitive element, 
//             // surface, sampler2d or texture). In this case, the current attribute is a compound
//             // attribute and holds for every index a child element.
//             if ( extraPlug.isCompound () )
//             {
//                 // TODO Recursive call for the child plugs.
//                 String attributeName = extraAttribute.name ().asChar ();
//                 if ( !COLLADABU::Utils::equals ( attributeName, secondKey ) ) continue;
//                 if ( exportExtraData ( extraPlug, attributeName, baseExtraTechnique, extraSource, secondKey ) )
//                     closeExtraSource = true;
//                 continue;
//             }
// 
//             if ( secondKey )
//             {
//                 // Get the physical index of the element.
//                 String physicalIndexStr = parentAttributeName.substr ( parentAttributeName.length ()-1 );
//                 unsigned int physicalIndex = atoi ( physicalIndexStr.c_str () );
//             }
// 
//             // The attribute with the collada profile name.
//             // The profile name has always the path attribute name in front.
//             String profileName = extraAttribute.name ().asChar ();
//             profileName = profileName.substr ( parentAttributeName.length () + 1 );
// 
//             // The attribute value.
//             MString mAttributeValue;
//             status = extraPlug.getValue ( mAttributeValue );
//             if ( status != MStatus::kSuccess || mAttributeValue == 0 ) continue;
// 
//             // Convert the attributeValue back to collada.
//             String source = mAttributeValue.asChar ();
//             COLLADABU::Utils::stringFindAndReplace ( source, "\\\"", "\"" );
//             String encodedSource = COLLADABU::URI::uriDecode ( source );
// 
//             // If a baseExtraTechnique element is given, use this to write the extra data.
//             if ( baseExtraTechnique )
//                 baseExtraTechnique->addExtraTechniqueTextblock ( profileName, encodedSource );
//             else
//             {
//                 // Open the extra source tag, if neccessary.
//                 if ( !closeExtraSource ) 
//                 {
//                     extraSource.openExtra();
//                     closeExtraSource = true;
//                 }
//                 // Create the extra attribute.
//                 COLLADASW::Technique techniqueSource ( &mStreamWriter );
//                 techniqueSource.openTechnique ( profileName );
//                 techniqueSource.addValue ( encodedSource );
//                 techniqueSource.closeTechnique();
//             }
//         }
// 
//         return closeExtraSource;
//     }

#endif//AD_IGNORE_MODIFY


}
