#include "mesh3d.h"

#include "../../../utility/math/matrix.h"
#include "../../../resource/resourceManager.h"

using namespace chira;

void mesh3d::render(const glm::mat4& parentTransform) {
    this->mesh->render(transformToMatrix(parentTransform, this->position, this->rotation));
    entity3d::render(parentTransform);
}

mesh3d::~mesh3d() {
    resourceManager::removeResource(this->mesh->getIdentifier());
}
