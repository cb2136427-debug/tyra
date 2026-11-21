#include <tyra>
#include <cmath>
#include <vector>

using namespace Tyra;

class CrossStrikeMasterPro : public Game {
 public:
  CrossStrikeMasterPro(Engine* engine) : engine(engine) {}

  void init() override {
    engine->renderer.setClearScreenColor(Color(110.0f, 150.0f, 195.0f));

    ObjLoaderOptions knifeOpts;
    knifeOpts.scale = 0.85f;
    knifeOpts.flipUVs = true;
    knifeData = ObjLoader::load(FileUtils::fromCwd("knife.obj"), knifeOpts);
    knifeMesh = std::make_unique<StaticMesh>(knifeData.get());

    buildProTestArena();

    playerPos = Vec4(0.0f, 0.0f, 0.0f);
    eyeHeight = standHeight;
  }

  void loop() override {
    float dt = 1.0f / 60.0f;
    const auto& pad = engine->pad;

    // 1. Camara (Stick Derecho)
    float rightH = (pad.getRightJoyPad().h - 128) / 128.0f;
    float rightV = (pad.getRightJoyPad().v - 128) / 128.0f;
    if (std::abs(rightH) < 0.14f) rightH = 0.0f;
    if (std::abs(rightV) < 0.14f) rightV = 0.0f;

    float lookSens = 0.048f;
    yaw -= rightH * lookSens;
    pitch -= rightV * lookSens;
    if (pitch > 1.38f) pitch = 1.38f;
    if (pitch < -1.38f) pitch = -1.38f;

    Vec4 forward(std::sin(yaw), 0.0f, std::cos(yaw));
    Vec4 right(std::cos(yaw), 0.0f, -std::sin(yaw));

    // 2. Movimiento (Stick Izquierdo)
    float moveZ = -(pad.getLeftJoyPad().v - 128) / 128.0f;
    float moveX = (pad.getLeftJoyPad().h - 128) / 128.0f;
    if (std::abs(moveZ) < 0.14f) moveZ = 0.0f;
    if (std::abs(moveX) < 0.14f) moveX = 0.0f;

    bool isMoving = (std::abs(moveZ) > 0.1f || std::abs(moveX) > 0.1f);
    bool isSprinting = (pad.getPressed().L3 || pad.getPressed().L1) && moveZ > 0.3f && !isCrouching;

    // 3. Salto y Slide-Cancel (X)
    if (pad.getClicked().Cross && isGrounded) {
      verticalVelocity = jumpForce;
      isGrounded = false;
      if (isSliding) {
        isSliding = false;
        slideJumpBoost = 1.35f;
      }
    }

    verticalVelocity -= gravity * dt;
    playerPos.y += verticalVelocity * dt;

    if (playerPos.y <= 0.0f) {
      playerPos.y = 0.0f;
      verticalVelocity = 0.0f;
      isGrounded = true;
      slideJumpBoost = 1.0f;
    }

    // 4. Deslizamiento / Slide (Circulo)
    if (pad.getClicked().Circle && isGrounded && !isSliding && moveZ > 0.45f) {
      isSliding = true;
      isCrouching = false;
      slideTimer = 0.68f;
      slideDir = forward;
    }

    if (isSliding) {
      slideTimer -= dt;
      eyeHeight += (crouchHeight - eyeHeight) * 14.0f * dt;
      float slideDecel = 0.65f + (slideTimer / 0.68f) * 0.35f;
      playerPos += slideDir * (sprintSpeed * 1.65f * slideDecel) * dt;

      if (slideTimer <= 0.0f) {
        isSliding = false;
      }
    } else {
      if (pad.getClicked().R3 || (pad.getClicked().Circle && moveZ <= 0.45f)) {
        isCrouching = !isCrouching;
      }

      float targetHeight = isCrouching ? crouchHeight : standHeight;
      eyeHeight += (targetHeight - eyeHeight) * 11.0f * dt;

      float speed = isCrouching ? crouchSpeed : (isSprinting ? sprintSpeed : walkSpeed);
      speed *= slideJumpBoost;

      Vec4 wishDir = forward * moveZ + right * moveX;
      playerPos += wishDir * speed * dt;
    }

    // 5. Actualizar Camara
    Vec4 camPos = playerPos + Vec4(0.0f, eyeHeight, 0.0f);
    Vec4 lookDir(std::sin(yaw) * std::cos(pitch),
                 std::sin(pitch),
                 std::cos(yaw) * std::cos(pitch));

    engine->renderer.getCamera().position = camPos;
    engine->renderer.getCamera().lookAt = camPos + lookDir;

    // 6. Cuchillo en mano (Balanceo + Ataque R1)
    if (isMoving && isGrounded && !isSliding) {
      bobTimer += dt * (isSprinting ? 15.0f : 9.5f);
    } else {
      bobTimer = 0.0f;
    }

    if (pad.getClicked().R1 && attackTimer <= 0.0f) {
      attackTimer = 0.25f;
    }
    if (attackTimer > 0.0f) {
      attackTimer -= dt;
    }

    float bobX = std::cos(bobTimer) * 0.065f;
    float bobY = std::abs(std::sin(bobTimer)) * 0.065f;
    float slashForward = (attackTimer > 0.0f) ? std::sin((attackTimer / 0.25f) * 3.1415f) * 0.85f : 0.0f;

    Vec4 weaponOffset = forward * (2.1f + slashForward) + right * (0.95f + bobX) + Vec4(0.0f, -0.85f - bobY, 0.0f);
    knifeMesh->translation = camPos + weaponOffset;
    knifeMesh->rotation.y = yaw + 1.57f;
    knifeMesh->rotation.x = -pitch * 0.6f - (slashForward * 0.5f);
    knifeMesh->rotation.z = isSliding ? 0.50f : 0.0f;

    // 7. Renderizar
    engine->renderer.beginFrame();
    engine->renderer.renderer3D.usePipeline(staticPipeline);

    for (auto& block : arenaMeshes) {
      staticPipeline.render(block.get());
    }
    staticPipeline.render(knifeMesh.get());

    engine->renderer.endFrame();
  }

 private:
  void buildProTestArena() {
    for (int x = -5; x <= 5; x++) {
      for (int z = -5; z <= 5; z++) {
        auto tileData = std::make_unique<MeshData>();
        float size = 30.0f;
        float px = x * size;
        float pz = z * size;

        tileData->vertices = {
            Vec4(px, 0.0f, pz),
            Vec4(px + size, 0.0f, pz),
            Vec4(px + size, 0.0f, pz + size),
            Vec4(px, 0.0f, pz),
            Vec4(px + size, 0.0f, pz + size),
            Vec4(px, 0.0f, pz + size)
        };

        auto tileMesh = std::make_unique<StaticMesh>(tileData.get());
        arenaData.push_back(std::move(tileData));
        arenaMeshes.push_back(std::move(tileMesh));
      }
    }
  }

  Engine* engine;
  StaticPipeline staticPipeline;

  std::unique_ptr<MeshData> knifeData;
  std::unique_ptr<StaticMesh> knifeMesh;

  std::vector<std::unique_ptr<MeshData>> arenaData;
  std::vector<std::unique_ptr<StaticMesh>> arenaMeshes;

  Vec4 playerPos;
  Vec4 slideDir;
  float yaw = 0.0f, pitch = 0.0f;

  const float standHeight = 10.0f;
  const float crouchHeight = 5.0f;
  float eyeHeight = 10.0f;

  const float walkSpeed = 28.0f;
  const float sprintSpeed = 46.0f;
  const float crouchSpeed = 14.0f;
  const float jumpForce = 33.0f;
  const float gravity = 78.0f;

  float verticalVelocity = 0.0f;
  float slideJumpBoost = 1.0f;
  bool isGrounded = true;
  bool isCrouching = false;
  bool isSliding = false;
  float slideTimer = 0.0f;
  float bobTimer = 0.0f;
  float attackTimer = 0.0f;
};

int main() {
  Tyra::EngineOptions options;
  options.loadUsbDriver = true; // Carga drivers USB para leer knife.obj desde pendrive en PS2 real
  Engine engine(options);
  CrossStrikeMasterPro game(&engine);
  engine.run(&game);
  return 0;
}
