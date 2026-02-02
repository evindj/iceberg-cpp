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
#include "iceberg/util/uuid.h"

namespace iceberg {

// Test boolean constant expressions
TEST(ExpressionJsonTest, CheckBooleanExpression) {
  auto checkBoolean = [](std::shared_ptr<Expression> expr, bool value) {
    auto json = ToJson(*expr);
    EXPECT_TRUE(json.is_boolean());
    EXPECT_EQ(json.get<bool>(), value);

    auto result = ExpressionFromJson(json);
    ASSERT_THAT(result, IsOk());
    if (value) {
      EXPECT_EQ(result.value()->op(), Expression::Operation::kTrue);
    } else {
      EXPECT_EQ(result.value()->op(), Expression::Operation::kFalse);
    }
  };
  checkBoolean(True::Instance(), true);
  checkBoolean(False::Instance(), false);
}

TEST(ExpressionJsonTest, OperationTypeTests) {
  EXPECT_EQ(OperationTypeFromJson("true"), Expression::Operation::kTrue);
  EXPECT_EQ("true", ToJson(Expression::Operation::kTrue));
  EXPECT_TRUE(IsSetOperation(Expression::Operation::kIn));
  EXPECT_FALSE(IsSetOperation(Expression::Operation::kTrue));

  EXPECT_TRUE(IsUnaryOperation(Expression::Operation::kIsNull));
  EXPECT_FALSE(IsUnaryOperation(Expression::Operation::kTrue));
}

TEST(ExpressionJsonTest, NameReferenceRoundTrip) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref, NamedReference::Make("col_name"));
  auto json = ToJson(*ref);
  EXPECT_EQ(json.get<std::string>(), "col_name");

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, NamedReferenceFromJson(json));
  EXPECT_EQ(parsed->name(), "col_name");
}

TEST(ExpressionJsonTest, UnboundTransfromRoundTrip) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref, NamedReference::Make("ts"));
  auto transform = Transform::Day();
  ICEBERG_UNWRAP_OR_FAIL(auto unbound, UnboundTransform::Make(std::move(ref), transform));

  auto json = ToJson(*unbound);
  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "day");
  EXPECT_EQ(json["term"], "ts");

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, UnboundTransformFromJson(json));
  EXPECT_EQ(parsed->reference()->name(), unbound->reference()->name());
  EXPECT_EQ(parsed->transform()->transform_type(),
            unbound->transform()->transform_type());
  EXPECT_EQ(parsed->transform()->ToString(), unbound->transform()->ToString());
}

TEST(ExpressionJsonTest, BucketTransform) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref, NamedReference::Make("id"));
  ICEBERG_UNWRAP_OR_FAIL(auto unbound,
                         UnboundTransform::Make(std::move(ref), Transform::Bucket(16)));

  auto json = ToJson(*unbound);
  EXPECT_EQ(json["type"], "transform");
  EXPECT_EQ(json["transform"], "bucket[16]");
  EXPECT_EQ(json["term"], "id");

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, UnboundTransformFromJson(json));
  EXPECT_EQ(parsed->transform()->transform_type(),
            unbound->transform()->transform_type());
  EXPECT_EQ(parsed->transform()->ToString(), unbound->transform()->ToString());
}

TEST(ExpressionJsonTest, InvalidInput) {
  EXPECT_THAT(UnboundTransformFromJson(nlohmann::json::object()),
              IsError(ErrorKind::kJsonParseError));
  EXPECT_THAT(UnboundTransformFromJson(nlohmann::json{{"type", "other"}}),
              IsError(ErrorKind::kJsonParseError));
}

TEST(ExpressionJsonTest, LiteralPrimitiveTypes) {
  // Boolean
  EXPECT_EQ(ToJson(Literal::Boolean(true)), true);
  EXPECT_EQ(ToJson(Literal::Boolean(false)), false);

  // Int and Long
  EXPECT_EQ(ToJson(Literal::Int(42)), 42);
  EXPECT_EQ(ToJson(Literal::Long(123456789012345LL)), 123456789012345LL);

  // Float and Double
  EXPECT_FLOAT_EQ(ToJson(Literal::Float(3.14f)).get<float>(), 3.14f);
  EXPECT_DOUBLE_EQ(ToJson(Literal::Double(2.718281828)).get<double>(), 2.718281828);

  // String
  EXPECT_EQ(ToJson(Literal::String("hello")), "hello");

  // Null
  EXPECT_TRUE(ToJson(Literal::Null(nullptr)).is_null());
}

TEST(ExpressionJsonTest, LiteralTemporalTypes) {
  // Date: ISO format string (e.g., 19738 = 2024-01-16)
  EXPECT_EQ(ToJson(Literal::Date(19738)), "2024-01-16");

  // Time: ISO format string (e.g., 52200000000 = 14:30)
  EXPECT_EQ(ToJson(Literal::Time(52200000000LL)), "14:30");

  // Timestamp: ISO format string
  EXPECT_EQ(ToJson(Literal::Timestamp(1705329000000000LL)), "2024-01-15T14:30:00");
}

TEST(ExpressionJsonTest, LiteralComplexTypes) {
  // UUID
  auto uuid_result = Uuid::FromString("550e8400-e29b-41d4-a716-446655440000");
  ASSERT_THAT(uuid_result, IsOk());
  EXPECT_EQ(ToJson(Literal::UUID(*uuid_result)), "550e8400-e29b-41d4-a716-446655440000");

  // Binary (raw hex, no prefix - matches Java format)
  std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f};  // "Hello"
  EXPECT_EQ(ToJson(Literal::Binary(data)), "48656C6C6F");

  // Decimal
  auto decimal_json = ToJson(Literal::Decimal(123456, 6, 3));
  EXPECT_EQ(decimal_json, "123.456");
}

TEST(ExpressionJsonTest, LiteralFromJsonPrimitives) {
  // Boolean
  ICEBERG_UNWRAP_OR_FAIL(auto bool_lit, LiteralFromJson(true));
  EXPECT_EQ(std::get<bool>(bool_lit.value()), true);

  // Integer -> Long
  ICEBERG_UNWRAP_OR_FAIL(auto int_lit, LiteralFromJson(42));
  EXPECT_EQ(std::get<int64_t>(int_lit.value()), 42);

  // Float -> Double
  ICEBERG_UNWRAP_OR_FAIL(auto float_lit, LiteralFromJson(3.14));
  EXPECT_DOUBLE_EQ(std::get<double>(float_lit.value()), 3.14);

  // String
  ICEBERG_UNWRAP_OR_FAIL(auto str_lit, LiteralFromJson("hello"));
  EXPECT_EQ(std::get<std::string>(str_lit.value()), "hello");

  // Hex string stays as string (conversion to binary happens during binding)
  ICEBERG_UNWRAP_OR_FAIL(auto hex_lit, LiteralFromJson("48656C6C6F"));
  EXPECT_EQ(std::get<std::string>(hex_lit.value()), "48656C6C6F");

  // Null
  ICEBERG_UNWRAP_OR_FAIL(auto null_lit, LiteralFromJson(nullptr));
  EXPECT_TRUE(null_lit.IsNull());
}

TEST(ExpressionJsonTest, LiteralFromJsonInvalid) {
  EXPECT_THAT(LiteralFromJson(nlohmann::json::array({1, 2, 3})),
              IsError(ErrorKind::kJsonParseError));
  EXPECT_THAT(LiteralFromJson(nlohmann::json::object()),
              IsError(ErrorKind::kJsonParseError));
}

TEST(ExpressionJsonTest, UnaryPredicate) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref_ptr, NamedReference::Make("col"));
  auto ref = std::shared_ptr<NamedReference>(std::move(ref_ptr));
  ICEBERG_UNWRAP_OR_FAIL(auto pred, UnboundPredicateImpl<BoundReference>::Make(
                                        Expression::Operation::kIsNull, ref));

  auto json = ToJson(*pred);
  EXPECT_EQ(json["type"], "is-null");
  EXPECT_EQ(json["term"], "col");
  EXPECT_FALSE(json.contains("value"));

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, ExpressionFromJson(json));
  EXPECT_EQ(parsed->op(), pred->op());
}

TEST(ExpressionJsonTest, LiteralPredicate) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref_ptr, NamedReference::Make("age"));
  auto ref = std::shared_ptr<NamedReference>(std::move(ref_ptr));
  ICEBERG_UNWRAP_OR_FAIL(
      auto pred, UnboundPredicateImpl<BoundReference>::Make(Expression::Operation::kGt,
                                                            ref, Literal::Long(21)));

  auto json = ToJson(*pred);
  EXPECT_EQ(json["type"], "gt");
  EXPECT_EQ(json["term"], "age");
  EXPECT_EQ(json["value"], 21);

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, ExpressionFromJson(json));
  EXPECT_EQ(parsed->op(), pred->op());
}

TEST(ExpressionJsonTest, SetPredicate) {
  ICEBERG_UNWRAP_OR_FAIL(auto ref_ptr, NamedReference::Make("status"));
  auto ref = std::shared_ptr<NamedReference>(std::move(ref_ptr));
  std::vector<Literal> values = {Literal::String("active"), Literal::String("pending")};
  ICEBERG_UNWRAP_OR_FAIL(
      auto pred, UnboundPredicateImpl<BoundReference>::Make(Expression::Operation::kIn,
                                                            ref, std::move(values)));

  auto json = ToJson(*pred);
  EXPECT_EQ(json["type"], "in");
  EXPECT_EQ(json["term"], "status");
  EXPECT_TRUE(json["values"].is_array());
  EXPECT_EQ(json["values"].size(), 2);

  ICEBERG_UNWRAP_OR_FAIL(auto parsed, ExpressionFromJson(json));
  EXPECT_EQ(parsed->op(), Expression::Operation::kIn);
}

TEST(ExpressionJsonTest, AndExpression) {
  nlohmann::json json = {{"type", "and"},
                         {"left", {{"type", "gt"}, {"term", "age"}, {"value", 18}}},
                         {"right", {{"type", "lt"}, {"term", "age"}, {"value", 65}}}};

  ICEBERG_UNWRAP_OR_FAIL(auto expr, ExpressionFromJson(json));
  EXPECT_EQ(expr->op(), Expression::Operation::kAnd);

  auto round_trip = ToJson(*expr);
  EXPECT_EQ(round_trip["type"], "and");
  EXPECT_EQ(round_trip["left"]["type"], "gt");
  EXPECT_EQ(round_trip["right"]["type"], "lt");
  EXPECT_EQ(round_trip["left"]["term"], "age");
  EXPECT_EQ(round_trip["right"]["term"], "age");
  EXPECT_EQ(round_trip["left"]["value"], 18);
  EXPECT_EQ(round_trip["right"]["value"], 65);
}

TEST(ExpressionJsonTest, NotExpression) {
  nlohmann::json json = {{"type", "not"},
                         {"child", {{"type", "is-null"}, {"term", "name"}}}};

  ICEBERG_UNWRAP_OR_FAIL(auto expr, ExpressionFromJson(json));
  EXPECT_EQ(expr->op(), Expression::Operation::kNot);

  auto round_trip = ToJson(*expr);
  EXPECT_EQ(round_trip["type"], "not");
  EXPECT_EQ(round_trip["child"]["type"], "is-null");
  EXPECT_EQ(round_trip["child"]["term"], "name");
}

TEST(ExpressionJsonTest, TransformPredicate) {
  // Test predicate with transform term: day(ts) = 19738
  nlohmann::json json = {
      {"type", "eq"},
      {"term", {{"type", "transform"}, {"transform", "day"}, {"term", "ts"}}},
      {"value", 19738}};

  ICEBERG_UNWRAP_OR_FAIL(auto expr, ExpressionFromJson(json));
  EXPECT_EQ(expr->op(), Expression::Operation::kEq);

  auto round_trip = ToJson(*expr);
  EXPECT_EQ(round_trip["type"], "eq");
  EXPECT_EQ(round_trip["term"]["type"], "transform");
  EXPECT_EQ(round_trip["term"]["transform"], "day");
  EXPECT_EQ(round_trip["term"]["term"], "ts");
  EXPECT_EQ(round_trip["value"], 19738);
}

TEST(ExpressionJsonTest, TransformPredicateRoundTrip) {
  // Create transform predicate programmatically and verify round-trip
  ICEBERG_UNWRAP_OR_FAIL(auto ref, NamedReference::Make("timestamp_col"));
  ICEBERG_UNWRAP_OR_FAIL(
      auto transform,
      UnboundTransform::Make(std::shared_ptr<NamedReference>(std::move(ref)),
                             Transform::Year()));
  ICEBERG_UNWRAP_OR_FAIL(auto pred, UnboundPredicateImpl<BoundTransform>::Make(
                                        Expression::Operation::kGt, std::move(transform),
                                        Literal::Int(2020)));

  auto json = ToJson(*pred);
  EXPECT_EQ(json["type"], "gt");
  EXPECT_EQ(json["term"]["type"], "transform");
  EXPECT_EQ(json["term"]["transform"], "year");
  EXPECT_EQ(json["term"]["term"], "timestamp_col");
  EXPECT_EQ(json["value"], 2020);

  // Parse back and verify
  ICEBERG_UNWRAP_OR_FAIL(auto parsed, ExpressionFromJson(json));
  EXPECT_EQ(parsed->op(), Expression::Operation::kGt);
}

}  // namespace iceberg
