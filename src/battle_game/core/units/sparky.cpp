#define GLM_ENABLE_EXPERIMENTAL
#include "battle_game/core/units/sparky.h"

#include "battle_game/core/bullets/bullets.h"
#include "battle_game/core/game_core.h"
#include "battle_game/graphics/graphics.h"
#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp> // glm::rotate

namespace battle_game::unit {

namespace {
uint32_t tank_body_model_index = 0xffffffffu;
uint32_t tank_turret_model_index = 0xffffffffu;
uint32_t tank_cannon_model_index = 0xffffffffu;
const float kMaxHeat = 100.0f;  // Maximum heat before the barrel is disabled
const float kHeatPerShot = 20.0f;  // Heat generated per shot
const float kHeatDissipationRate = 2.0f;  // Heat dissipation rate per second
}  // namespace

Sparky::Sparky(GameCore *game_core, uint32_t id, uint32_t player_id)
    : Unit(game_core, id, player_id) {
  if (!~tank_body_model_index) {
    auto mgr = AssetsManager::GetInstance();

    // Tank Body (车身，红色部分)
    {
      tank_body_model_index = mgr->RegisterModel(
          {
              // 顶部区域（蓝色）
              {{-0.8f, 0.8f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},  // 左上角
              {{-0.8f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}}, // 左下角
              {{0.8f, 0.8f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},   // 右上角
              {{0.8f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},  // 右下角

              // 前部标识部分（红色，区分前后）
              {{0.6f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},   // 前右角
              {{-0.6f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},  // 前左角
          },
          {0, 1, 2, 1, 2, 3, 0, 2, 5, 2, 4, 5});
    }

    // Tank Turret (炮塔，黄色环部分)
    {
      std::vector<ObjectVertex> turret_vertices;
      std::vector<uint32_t> turret_indices;
      const int precision = 6;  // 环的精度
      const float inv_precision = 1.0f / float(precision);

      // 环的顶点定义
      for (int i = 0; i < precision; i++) {
        float theta = glm::pi<float>() * 2.0f * (float(i) * inv_precision);
        float sin_theta = std::sin(theta);
        float cos_theta = std::cos(theta);

        // 外环顶点（黄色）
        turret_vertices.push_back(
            {{sin_theta * 0.6f, cos_theta * 0.6f},
             {0.0f, 0.0f},
             {1.0f, 1.0f, 0.0f, 1.0f}});  // 黄色

        // 内环顶点（黑色）
        turret_vertices.push_back(
            {{sin_theta * 0.4f, cos_theta * 0.4f},
             {0.0f, 0.0f},
             {0.0f, 0.0f, 0.0f, 1.0f}});  // 黑色

        // 三角形索引
        turret_indices.push_back(i * 2);
        turret_indices.push_back((i * 2 + 1) % (precision * 2));
        turret_indices.push_back((i * 2 + 2) % (precision * 2));

        turret_indices.push_back((i * 2 + 1) % (precision * 2));
        turret_indices.push_back((i * 2 + 2) % (precision * 2));
        turret_indices.push_back((i * 2 + 3) % (precision * 2));
      }

      tank_turret_model_index =
          mgr->RegisterModel(turret_vertices, turret_indices);
    }
    {
      std::vector<ObjectVertex> cannon_vertices = {
          {{-0.1f, -0.4f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},  // 左下角
          {{0.1f, -0.4f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},   // 右下角
          {{-0.1f, 1.2f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},    // 左上角
          {{0.1f, 1.2f}, {0.0f, 0.0f}, {0.0f, 0.5f, 1.0f, 1.0f}},     // 右上角
      };

      std::vector<uint32_t> cannon_indices = {
          0, 1, 2, 1, 2, 3,  // 两个三角形组成炮管矩形
      };

      tank_cannon_model_index = mgr->RegisterModel(cannon_vertices, cannon_indices);
    }
  }
}



void Sparky::Update() {
  TankMove(2.0f, glm::radians(180.0f));
  TurretRotate();
  Fire();

  // Update position based on velocity
  position_ += velocity_ * kSecondPerTick;

  // Apply friction to gradually reduce the velocity
  float friction = 0.995f;
  velocity_ *= friction;

  // Dissipate heat over time
  heat_ = std::max(0.0f, heat_ - kHeatDissipationRate * kSecondPerTick);

  // Check for weapon type switch
  auto player = game_core_->GetPlayer(player_id_);
  if (player) {
    auto &input_data = player->GetInputData();
    if (input_data.key_down[GLFW_KEY_H]) {
      current_weapon_type_ = current_weapon_type_ == CANNON ? MACHINE_GUN : CANNON;
    }
  }
}

void Sparky::TankMove(float move_speed, float rotate_angular_speed) {
  auto player = game_core_->GetPlayer(player_id_);
  if (player) {
    auto &input_data = player->GetInputData();
    glm::vec2 desired_velocity{0.0f};
    if (input_data.key_down[GLFW_KEY_W]) {
      desired_velocity.y += 1.0f;
    }
    if (input_data.key_down[GLFW_KEY_S]) {
      desired_velocity.y -= 1.0f;
    }
    float speed = move_speed * GetSpeedScale();
    desired_velocity *= speed;

    // Calculate acceleration
    glm::vec2 acceleration = (desired_velocity - velocity_) * 4.0f;  // Adjust 0.1f for acceleration rate
    velocity_ += acceleration * kSecondPerTick;

    auto new_position =
        position_ + glm::vec2{glm::rotate(glm::mat4{1.0f}, rotation_,
                                          glm::vec3{0.0f, 0.0f, 1.0f}) *
                              glm::vec4{velocity_ * kSecondPerTick, 0.0f, 0.0f}};
    if (!game_core_->IsBlockedByObstacles(new_position)) {
      game_core_->PushEventMoveUnit(id_, new_position);
    }

    float rotation_offset = 0.0f;
    if (input_data.key_down[GLFW_KEY_A]) {
      rotation_offset += 1.0f;
    }
    if (input_data.key_down[GLFW_KEY_D]) {
      rotation_offset -= 1.0f;
    }
    // Apply a constant small angular acceleration
    float angular_acceleration = 0.004f;  // Adjust this value for desired angular acceleration
    rotation_offset *= angular_acceleration * rotate_angular_speed * GetSpeedScale();
    game_core_->PushEventRotateUnit(id_, rotation_ + rotation_offset);
  }
}

void Sparky::TurretRotate() {
  auto player = game_core_->GetPlayer(player_id_);
  if (player) {
    auto &input_data = player->GetInputData();
    auto diff = input_data.mouse_cursor_position - position_;
    float target_rotation;
    if (glm::length(diff) < 1e-4) {
      target_rotation = rotation_;
    } else {
      target_rotation = std::atan2(diff.y, diff.x) - glm::radians(90.0f);
    }
    
    // Ensure the shortest path is taken
    float delta_angle = glm::mod(target_rotation - turret_rotation_ + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
    
    float start_angular_acceleration = 2.0f;  // Angular acceleration at the start
    float end_angular_acceleration = 20.0f;  // Angular acceleration at the end

    // Determine rotation speed based on distance to target angle
    if (std::abs(delta_angle) < 0.01f) {
      rotation_speed = std::max(rotation_speed-end_angular_acceleration * kSecondPerTick , 0.0f);
    } else {
      rotation_speed = std::min(rotation_speed+start_angular_acceleration * kSecondPerTick , 3.0f);
    }
    // Apply rotation
    float t = rotation_speed * kSecondPerTick;
    turret_rotation_ += delta_angle * t;
  }
}

void Sparky::Fire() {
  if (fire_count_down_ == 0 && heat_ < kMaxHeat) {
    auto player = game_core_->GetPlayer(player_id_);
    if (player) {
      auto &input_data = player->GetInputData();
      if (input_data.mouse_button_down[GLFW_MOUSE_BUTTON_LEFT]) {
        auto bullet_velocity = Rotate(glm::vec2{0.0f, 50.0f}, turret_rotation_);
        glm::vec2 bullet_position = position_ + Rotate({0.0f, 1.2f}, turret_rotation_);
        
        // Calculate distance to target
        glm::vec2 target_position = input_data.mouse_cursor_position;
        float distance = glm::length(target_position - bullet_position);
        
        // Adjust damage based on distance
        float base_damage = 8.0f;
        float damage_scale = GetDamageScale();
        float optimal_distance = 4.6f;  // Example optimal distance
        float distance_factor = std::exp(-3 * std::pow((distance - optimal_distance) / (optimal_distance), 2));
        float damage = base_damage * damage_scale * distance_factor;


        if (current_weapon_type_ == CANNON) {
          velocity_ -= Rotate(glm::vec2{0.0f, 50.0f}, turret_rotation_ - rotation_) * 0.008f;  // Recoil
          GenerateBullet<bullet::CannonBall>(
              bullet_position,
              turret_rotation_, damage, bullet_velocity * distance_factor);
          fire_count_down_ = 2 * kTickPerSecond;  // Fire interval 2 seconds.
          // Increase heat
          heat_ += kHeatPerShot;
        } else if (current_weapon_type_ == MACHINE_GUN) {
          GenerateBullet<bullet::CannonBall>(
              bullet_position,
              turret_rotation_, GetDamageScale() * 0.05f, 0.5f * bullet_velocity);  // Less damage, faster bullets
          fire_count_down_ = 0.1 * kTickPerSecond;  // Fire interval 0.1 seconds.
        }
      }
    }
  }
  if (fire_count_down_) {
    fire_count_down_--;
  }
}

bool Sparky::IsHit(glm::vec2 position) const {
  position = WorldToLocal(position);
  return position.x > -0.8f && position.x < 0.8f && position.y > -1.0f &&
         position.y < 1.0f && position.x + position.y < 1.6f &&
         position.y - position.x < 1.6f;
}

void Sparky::Render() {
  // 设置坦克整体的变换（位置和旋转）
  battle_game::SetTransformation(position_, rotation_);

  // 绘制坦克车身（红色部分）
  battle_game::SetTexture(0);  // 不使用纹理
  battle_game::SetColor(game_core_->GetPlayerColor(player_id_));  // 玩家颜色
  battle_game::DrawModel(tank_body_model_index);  // 绘制车身模型

  // 绘制黄色环（炮塔部分）
  battle_game::SetRotation(turret_rotation_);  // 设置炮塔的旋转
  battle_game::DrawModel(tank_turret_model_index);  // 绘制炮塔模型

  // 绘制炮管（灰色部分）
  battle_game::SetColor(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));  // 灰色
  battle_game::DrawModel(tank_cannon_model_index);  // 绘制炮管模型
}


const char *Sparky::UnitName() const {
  return "Sparky";
}

const char *Sparky::Author() const {
  return "Mini Pekka";
}

}  // namespace battle_game::unit