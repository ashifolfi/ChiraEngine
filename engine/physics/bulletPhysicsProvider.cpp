#include "bulletPhysicsProvider.h"

#include <core/engine.h>

using namespace chira;

BulletPhysicsProvider::BulletPhysicsProvider() {
    this->collisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>(btDefaultCollisionConstructionInfo());
    this->dispatcher = std::make_unique<btCollisionDispatcher>(this->collisionConfiguration.get());
    this->overlappingPairCache = std::make_unique<btDbvtBroadphase>();
    this->solver = std::make_unique<btSequentialImpulseConstraintSolver>();
    this->dynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(
            this->dispatcher.get(),
            this->overlappingPairCache.get(),
            this->solver.get(),
            this->collisionConfiguration.get());
    // Don't use bulletPhysicsProvider::setGravity because virtual method shouldn't be used in constructor
    this->dynamicsWorld->setGravity(btVector3(0, -12, 0));
}

void BulletPhysicsProvider::updatePhysics(double delta) {
    int step = 4;
    Engine::getSettingsLoader()->getValue("physics", "subStep", &step);
    this->dynamicsWorld->stepSimulation((btScalar) delta, step);
}

void BulletPhysicsProvider::stop() {
    for (int i = this->dynamicsWorld->getNumCollisionObjects() - 1; i >= 0; i--) {
        btCollisionObject* obj = this->dynamicsWorld->getCollisionObjectArray()[i];
        btRigidBody* body = btRigidBody::upcast(obj);
        if (body && body->getMotionState()) {
            delete body->getMotionState();
        }
        this->dynamicsWorld->removeCollisionObject(obj);
        delete obj;
    }
}

void BulletPhysicsProvider::addRigidBody(btRigidBody* rb) {
    this->dynamicsWorld->addRigidBody(rb);
}

void BulletPhysicsProvider::removeRigidBody(btRigidBody* rb) {
    this->dynamicsWorld->removeRigidBody(rb);
}

void BulletPhysicsProvider::setGravity(const glm::vec3 gravity) {
    this->dynamicsWorld->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
}
