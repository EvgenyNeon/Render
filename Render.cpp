// MadRay frame rendering function 2020

bool renderingFrame(tCanvas* canvas, tMeshManager* meshManager, tMaterialManager* materialManager, tRCamera* camera) // First pass
{
	extent<2> resolutionCanvas(canvas->getResolution().y * canvas->getKMS(), canvas->getResolution().x * canvas->getKMS());
    array_view<Concurrency::graphics::float_3, 2> pixels(canvas->getResolution().y * canvas->getKMS(), canvas->getResolution().x * canvas->getKMS(), canvas->pixels);

    std::vector<Concurrency::graphics::float_3> vecValuesBuffer;
    std::vector<Concurrency::graphics::float_3> texValuesBuffer;
    std::vector<Concurrency::graphics::int_2> texResolutionsBuffer;
    std::vector<tRMaterial>* const materialsBuffer = materialManager->createPerfRender(&vecValuesBuffer, &texValuesBuffer, &texResolutionsBuffer);
    array_view<Concurrency::graphics::float_3, 1> vecValues(vecValuesBuffer.size(), vecValuesBuffer);
    array_view<Concurrency::graphics::float_3, 1> texValues(texValuesBuffer.size(), texValuesBuffer);
    array_view<Concurrency::graphics::int_2, 1> texResolutions(texResolutionsBuffer.size(), texResolutionsBuffer);
    array_view<tRMaterial, 1> materials(materialsBuffer->size(), *materialsBuffer);

    std::vector<tRTriangle>* trianglesBuffer = meshManager->createPerfRender();
    array_view<tRTriangle, 1> triangles(trianglesBuffer->size(), *trianglesBuffer);

    std::vector<tRCamera> bufferCamera;
    bufferCamera.push_back(*camera);
    array_view<tRCamera, 1> traceCamera(1, bufferCamera);

    parallel_for_each(resolutionCanvas, [=](index<2> idx) restrict(amp, cpu)
    {
		using namespace Concurrency::graphics;
        using namespace ampVecFunc;
        using namespace Concurrency::fast_math;

        float_2 pixelCoord = float_2(static_cast<float>(idx[1]), static_cast<float > (idx[0]));
        float_2 resolution = float_2(static_cast<float>(resolutionCanvas[1]), static_cast<float>(resolutionCanvas[0]));
        float aspectRatio = resolution.x / resolution.y;
        float_2 uvGlobal;
        uvGlobal.x = (pixelCoord.x / resolution.x - 0.5) * aspectRatio;
        uvGlobal.y = pixelCoord.y / resolution.x * aspectRatio - 0.5;
            
        uvGlobal *= !traceCamera[0].isPerspective ? traceCamera[0].kWeight : 1.0;
  
        float_3 rayRotation = traceCamera[0].isPerspective
			? ampVecFunc::normalize3(ampVecFunc::rotate3(ampVecFunc::normalize3(float_3(traceCamera[0].POW, uvGlobal.x, uvGlobal.y)), traceCamera[0].rotation))
            : ampVecFunc::normalize3(ampVecFunc::rotate3(float_3(1.0f, .0f, .0f), traceCamera[0].rotation));


        float_3 rayOrigin = traceCamera[0].isPerspective
            ? traceCamera[0].location
            : traceCamera[0].location + ampVecFunc::rotate3(float_3(0.0, uvGlobal.x, uvGlobal.y), traceCamera[0].location);
            
        float maxDistance = 10000000.0f;
        int samplesCount = 1;
        float_3 color;

        for (int i = 0; i < samplesCount; i++)
        {
			bool isCollision = false;
            float_3 location;
            float_3 normal;
            float_2 uv;
            unsigned int material;
            float distance = maxDistance;

            for (int j = 0; j < triangles.extent.size(); j++) //TriangleIntersection
            {
				float_3 localNormal = triangles[j].normal;
                bool localIsCollision;
                float localDistance;
                float_3 localLocation;
                float_2 localUV;
                    
                if (ampVecFunc::dot3(rayRotation, localNormal) > 0.0f)
                {
                    localIsCollision = false;
                    continue; 
                }

                Concurrency::graphics::float_3 e1 = triangles[j].b - triangles[j].a;
                Concurrency::graphics::float_3 e2 = triangles[j].c - triangles[j].a;
                Concurrency::graphics::float_3 pvec = ampVecFunc::cross(rayRotation, e2);

                float det = ampVecFunc::dot3(e1, pvec); 
                if (det < 1e-8 && det > -1e-8)
                {
                    localIsCollision = false;
                    continue; 
                }

                float inv_det = 1.0f / det; 
                Concurrency::graphics::float_3 tvec = rayOrigin - triangles[j].a;
                Concurrency::graphics::float_3 lvec = ampVecFunc::cross(tvec, e1); 
                localDistance = ampVecFunc::dot3(e2, lvec) * inv_det;
                localLocation = rayOrigin + localDistance * rayRotation;
                if (ampVecFunc::dot3(rayOrigin - localLocation, rayRotation) > 0.0)
                {
                    localIsCollision = false;
                    continue; 
                }

                localUV.x = ampVecFunc::dot3(tvec, pvec) * inv_det;
                if (localUV.x < 0.0 || localUV.x > 1.0)
                {
                    localIsCollision = false;
                    continue; 
                }

                localUV.y = ampVecFunc::dot3(rayRotation, lvec) * inv_det;
                if (localUV.y < 0.0 || localUV.x + localUV.y > 1.0)
                {
                    localIsCollision = false;
                    continue; 
                }
                                            
                if (localDistance < 0.0)
                {
                    localIsCollision = false;
                    continue; 
                }

                float AreaABC = ampVecFunc::dot3(localNormal, ampVecFunc::cross(e1, e2));
                float AreaPBC = ampVecFunc::dot3(localNormal, ampVecFunc::cross(triangles[j].b - localLocation, triangles[j].c - localLocation));
                float AreaPCA = ampVecFunc::dot3(localNormal, ampVecFunc::cross(triangles[j].c - localLocation, triangles[j].a - localLocation));
                float b = AreaPCA / AreaABC; float a = AreaPBC / AreaABC; 
                float c = 1.0f - a - b; 
                localUV = a * triangles[j].aUV + b * triangles[j].bUV + c * triangles[j].cUV;
                unsigned int localMaterial = triangles[j].materialIndex;
                localIsCollision = true;
                //END TriangleIntersection
                    

                if (localDistance < distance) //Depth clipping
                {
                    distance = localDistance;
                    isCollision = localIsCollision;
                    location = localLocation;
                    normal = localNormal;
                    uv = localUV;
                    material = localMaterial;

                    float_3 albedo = texValues[getGlobalIndex(texResolutions, materials[material].albedoIndex, uv)];
                    albedo.x = concurrency::fast_math::pow(albedo.x, 2.2f);
                    albedo.y = concurrency::fast_math::pow(albedo.y, 2.2f);
                    albedo.z = concurrency::fast_math::pow(albedo.z, 2.2f);

                    float_3 metalness = texValues[getGlobalIndex(texResolutions, materials[material].metalnessIndex, uv)];
                        
                    float_3 roughness = texValues[getGlobalIndex(texResolutions, materials[material].roughnessIndex, uv)];

                    color = 2.0f * CookTorrance::CookTorrance_GGX(normal, float_3(-1.0f, 0.0f, 0.0f), rayRotation, albedo, metalness, roughness);
                    color = color / (color + float_3(1.0f));
                    color.x = concurrency::fast_math::pow(color.x, 1.0f / 2.2f);
                    color.y = concurrency::fast_math::pow(color.y, 1.0f / 2.2f);
                    color.z = concurrency::fast_math::pow(color.z, 1.0f / 2.2f);
                    //color = -normal;
                }
            }
                
        }
           
        pixels[idx[0]][idx[1]] = ampVecFunc::clamp3(color, .0f, 1.0f);
            
    });
    pixels.synchronize();
    pixels.discard_data();
    return true;
}