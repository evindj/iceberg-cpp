/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "iceberg/expression/expression.h"
#include "iceberg/expression/expressions.h"
#include "iceberg/expression/json_serde_internal.h"
#include "iceberg/expression/literal.h"
#include "iceberg/expression/predicate.h"
#include "iceberg/expression/term.h"
#include "iceberg/test/matchers.h"
#include "iceberg/transform.h"

namespace iceberg {

class ExpressionJsonTest : public ::testing::Test {};

// Test boolean constant expressions
TEST_F(ExpressionJsonTest, TrueExpression) {
  auto expr = True::Instance();
  auto json = ToJson(*expr);

  // True should serialize as JSON boolean true
  EXPECT_TRUE(json.is_boolean());
  EXPECT_TRUE(json.get<bool>());

  // Parse back
  auto result = ExpressionFromJson(json);
  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(result.value()->op(), Expression::Operation::kTrue);
}

TEST_F(ExpressionJsonTest, FalseExpression) {
  auto expr = False::Instance();
  auto json = ToJson(*expr);

  // False should serialize as JSON boolean false
  EXPECT_TRUE(json.is_boolean());
  EXPECT_FALSE(json.get<bool>());

  // Parse back
  auto result = ExpressionFromJson(json);
  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(result.value()->op(), Expression::Operation::kFalse);
}

TEST_F(ExpressionJsonTest, OperationTypeTests) {
  EXPECT_EQ(OperationTypeFromString("true"), Expression::Operation::kTrue);
  EXPECT_EQ("true", ToStringOperationType(Expression::Operation::kTrue));
  EXPECT_TRUE(IsSetOperation(Expression::Operation::kIn));
  EXPECT_FALSE(IsSetOperation(Expression::Operation::kTrue));

  EXPECT_TRUE(IsUnaryOperation(Expression::Operation::kIsNull));
  EXPECT_FALSE(IsUnaryOperation(Expression::Operation::kTrue));
}

// ============================================================================
// NamedReference JSON Serialization Tests
// ============================================================================

TEST_F(ExpressionJsonTest, NamedReferenceToJsonSpecialCharacters) {
  auto ref_result = NamedReference::Make("field-name_123.true");
  ASSERT_THAT(ref_result, IsOk());
  auto& ref = *ref_result.value();

  auto json = NamedReferenceToJson(ref);

  EXPECT_TRUE(json.is_string());
  EXPECT_EQ(json.get<std::string>(), "field-name_123.true");
}

// ============================================================================
// NamedReference JSON Deserialization Tests
// ============================================================================

TEST_F(ExpressionJsonTest, NamedReferenceFromJsonObject) {
  nlohmann::json json = {{"type", "reference"}, {"term", "field1_part1-.field"}};

  auto result = NamedReferenceFromJson(json);

  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(result.value()->name(), "field1_part1-.field");
}

TEST_F(ExpressionJsonTest, NamedReferenceFromJsonInvalidObjectType) {
  nlohmann::json json = {{"type", "transform"}, {"term", "field_name"}};

  auto result = NamedReferenceFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, NamedReferenceFromJsonMissingTerm) {
  nlohmann::json json = {{"type", "reference"}};

  auto result = NamedReferenceFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, NamedReferenceFromJsonMissingType) {
  nlohmann::json json = {{"term", "field_name"}};

  auto result = NamedReferenceFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, NamedReferenceRoundTrip) {
  auto original = NamedReference::Make("test_field");
  ASSERT_THAT(original, IsOk());

  auto json = NamedReferenceToJson(*original.value());
  auto result = NamedReferenceFromJson(json);

  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(result.value()->name(), original.value()->name());
}

// ============================================================================
// UnboundTransform JSON Serialization Tests
// ============================================================================

TEST_F(ExpressionJsonTest, UnboundTransformToJsonIdentity) {
  auto ref = NamedReference::Make("timestamp_col");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Identity());
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_TRUE(json.is_object());
  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "identity");
  EXPECT_EQ(json["term"], "timestamp_col");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonYear) {
  auto ref = NamedReference::Make("timestamp_field");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Year());
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_TRUE(json.is_object());
  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "year");
  EXPECT_EQ(json["term"], "timestamp_field");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonMonth) {
  auto ref = NamedReference::Make("date_field");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Month());
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "month");
  EXPECT_EQ(json["term"], "date_field");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonDay) {
  auto ref = NamedReference::Make("datetime");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Day());
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "day");
  EXPECT_EQ(json["term"], "datetime");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonHour) {
  auto ref = NamedReference::Make("timestamp");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Hour());
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "hour");
  EXPECT_EQ(json["term"], "timestamp");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonBucket) {
  auto ref = NamedReference::Make("id");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Bucket(10));
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "bucket[10]");
  EXPECT_EQ(json["term"], "id");
}

TEST_F(ExpressionJsonTest, UnboundTransformToJsonTruncate) {
  auto ref = NamedReference::Make("name");
  ASSERT_THAT(ref, IsOk());
  auto transform_result =
      UnboundTransform::Make(std::move(ref.value()), Transform::Truncate(5));
  ASSERT_THAT(transform_result, IsOk());
  auto& transform = *transform_result.value();

  auto json = UnboundTransformToJson(transform);

  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "truncate[5]");
  EXPECT_EQ(json["term"], "name");
}

// ============================================================================
// UnboundTransform JSON Deserialization Tests
// ============================================================================

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonIdentity) {
  nlohmann::json json = {
      {"type", "transform"}, {"transform", "identity"}, {"term", "field_name"}};

  auto result = UnboundTransformFromJson(json);

  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(result.value()->reference()->name(), "field_name");
  EXPECT_EQ(result.value()->transform()->ToString(), "identity");
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonMissingType) {
  nlohmann::json json = {{"transform", "year"}, {"term", "field"}};

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonMissingTransform) {
  nlohmann::json json = {{"type", "transform"}, {"term", "field"}};

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonMissingTerm) {
  nlohmann::json json = {{"type", "transform"}, {"transform", "year"}};

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonInvalidType) {
  nlohmann::json json = {{"type", "reference"}, {"transform", "year"}, {"term", "field"}};

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonInvalidTransformName) {
  nlohmann::json json = {
      {"type", "transform"}, {"transform", "invalid_transform"}, {"term", "field"}};

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonNotObject) {
  nlohmann::json json = "not an object";

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformFromJsonArray) {
  nlohmann::json json = nlohmann::json::array();

  auto result = UnboundTransformFromJson(json);

  EXPECT_FALSE(result.has_value());
}

TEST_F(ExpressionJsonTest, UnboundTransformRoundTripAllTransforms) {
  // Test various transforms round-trip correctly
  std::vector<std::pair<std::string, std::shared_ptr<Transform>>> test_cases = {
      {"identity", Transform::Identity()},
      {"year", Transform::Year()},
      {"month", Transform::Month()},
      {"day", Transform::Day()},
      {"hour", Transform::Hour()},
      {"bucket[8]", Transform::Bucket(8)},
      {"truncate[20]", Transform::Truncate(20)}};

  for (const auto& [expected_name, transform] : test_cases) {
    SCOPED_TRACE("Testing transform: " + expected_name);

    auto ref = NamedReference::Make("test_field");
    ASSERT_THAT(ref, IsOk());
    auto unbound = UnboundTransform::Make(std::move(ref.value()), transform);
    ASSERT_THAT(unbound, IsOk());

    auto json = UnboundTransformToJson(*unbound.value());
    auto result = UnboundTransformFromJson(json);

    ASSERT_THAT(result, IsOk());
    EXPECT_EQ(result.value()->reference()->name(), "test_field");
    EXPECT_EQ(result.value()->transform()->ToString(), expected_name);
  }
}

}  // namespace iceberg
