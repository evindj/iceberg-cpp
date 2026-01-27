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

#include <format>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "iceberg/expression/expressions.h"
#include "iceberg/expression/json_serde_internal.h"
#include "iceberg/expression/literal.h"
#include "iceberg/transform.h"
#include "iceberg/util/json_util_internal.h"
#include "iceberg/util/macros.h"

namespace iceberg {
namespace {
// JSON fields

constexpr std::string_view kTerm = "term";
constexpr std::string_view kType = "type";
constexpr std::string_view kReference = "reference";
constexpr std::string_view kTransform = "transform";
// Expression type strings
constexpr std::string_view kTypeTrue = "true";
constexpr std::string_view kTypeFalse = "false";
constexpr std::string_view kTypeEq = "eq";
constexpr std::string_view kTypeAnd = "and";
constexpr std::string_view kTypeOr = "or";
constexpr std::string_view kTypeNot = "not";
constexpr std::string_view kTypeIn = "in";
constexpr std::string_view kTypeNotIn = "not-in";
constexpr std::string_view kTypeLt = "lt";
constexpr std::string_view kTypeLtEq = "lt-eq";
constexpr std::string_view kTypeGt = "gt";
constexpr std::string_view kTypeGtEq = "gt-eq";
constexpr std::string_view kTypeNotEq = "not-eq";
constexpr std::string_view kTypeStartsWith = "starts-with";
constexpr std::string_view kTypeNotStartsWith = "not-starts-with";
constexpr std::string_view kTypeIsNull = "is-null";
constexpr std::string_view kTypeNotNull = "not-null";
constexpr std::string_view kTypeIsNan = "is-nan";
constexpr std::string_view kTypeNotNan = "not-nan";
}  // namespace

/// Check if an operation is a unary predicate (no values)
bool IsUnaryOperation(Expression::Operation op) {
  switch (op) {
    case Expression::Operation::kIsNull:
    case Expression::Operation::kNotNull:
    case Expression::Operation::kIsNan:
    case Expression::Operation::kNotNan:
      return true;
    default:
      return false;
  }
}

/// Check if an operation is a set predicate (multiple values)
bool IsSetOperation(Expression::Operation op) {
  switch (op) {
    case Expression::Operation::kIn:
    case Expression::Operation::kNotIn:
      return true;
    default:
      return false;
  }
}

/// \brief Converts a JSON type string to an Expression::Operation.
///
/// \param typeStr The JSON type string
/// \return The corresponding Operation or an error if unknown
Result<Expression::Operation> OperationTypeFromString(const std::string_view typeStr) {
  if (typeStr == kTypeTrue) return Expression::Operation::kTrue;
  if (typeStr == kTypeFalse) return Expression::Operation::kFalse;
  if (typeStr == kTypeAnd) return Expression::Operation::kAnd;
  if (typeStr == kTypeOr) return Expression::Operation::kOr;
  if (typeStr == kTypeNot) return Expression::Operation::kNot;
  if (typeStr == kTypeEq) return Expression::Operation::kEq;
  if (typeStr == kTypeNotEq) return Expression::Operation::kNotEq;
  if (typeStr == kTypeLt) return Expression::Operation::kLt;
  if (typeStr == kTypeLtEq) return Expression::Operation::kLtEq;
  if (typeStr == kTypeGt) return Expression::Operation::kGt;
  if (typeStr == kTypeGtEq) return Expression::Operation::kGtEq;
  if (typeStr == kTypeIn) return Expression::Operation::kIn;
  if (typeStr == kTypeNotIn) return Expression::Operation::kNotIn;
  if (typeStr == kTypeIsNull) return Expression::Operation::kIsNull;
  if (typeStr == kTypeNotNull) return Expression::Operation::kNotNull;
  if (typeStr == kTypeIsNan) return Expression::Operation::kIsNan;
  if (typeStr == kTypeNotNan) return Expression::Operation::kNotNan;
  if (typeStr == kTypeStartsWith) return Expression::Operation::kStartsWith;
  if (typeStr == kTypeNotStartsWith) return Expression::Operation::kNotStartsWith;

  return JsonParseError("Unknown expression type: {}", typeStr);
}

/// \brief Converts an Expression::Operation to its JSON string representation.
///
/// \param op The operation to convert
/// \return The JSON type string (e.g., "eq", "lt-eq", "is-null")
std::string_view ToStringOperationType(Expression::Operation op) {
  switch (op) {
    case Expression::Operation::kTrue:
      return kTypeTrue;
    case Expression::Operation::kFalse:
      return kTypeFalse;
    case Expression::Operation::kAnd:
      return kTypeAnd;
    case Expression::Operation::kOr:
      return kTypeOr;
    case Expression::Operation::kNot:
      return kTypeNot;
    case Expression::Operation::kEq:
      return kTypeEq;
    case Expression::Operation::kNotEq:
      return kTypeNotEq;
    case Expression::Operation::kLt:
      return kTypeLt;
    case Expression::Operation::kLtEq:
      return kTypeLtEq;
    case Expression::Operation::kGt:
      return kTypeGt;
    case Expression::Operation::kGtEq:
      return kTypeGtEq;
    case Expression::Operation::kIn:
      return kTypeIn;
    case Expression::Operation::kNotIn:
      return kTypeNotIn;
    case Expression::Operation::kIsNull:
      return kTypeIsNull;
    case Expression::Operation::kNotNull:
      return kTypeNotNull;
    case Expression::Operation::kIsNan:
      return kTypeIsNan;
    case Expression::Operation::kNotNan:
      return kTypeNotNan;
    case Expression::Operation::kStartsWith:
      return kTypeStartsWith;
    case Expression::Operation::kNotStartsWith:
      return kTypeNotStartsWith;
    default:
      return "unknown";
  }
}

/// Parse a named reference from JSON
Result<std::unique_ptr<NamedReference>> NamedReferenceFromJson(
    const nlohmann::json& json) {
  // Handle string term (simple reference)
  if (json.is_string()) {
    ICEBERG_ASSIGN_OR_RAISE(auto name, GetTypedJsonValue<std::string>(json));
    ICEBERG_ASSIGN_OR_RAISE(auto ref, NamedReference::Make(std::move(name)));
    return ref;
  }

  // Handle object term
  if (json.is_object()) {
    ICEBERG_ASSIGN_OR_RAISE(auto typeStr, GetJsonValue<std::string>(json, kType));

    if (typeStr == kReference) {
      ICEBERG_ASSIGN_OR_RAISE(auto name, GetJsonValue<std::string>(json, kTerm));
      ICEBERG_ASSIGN_OR_RAISE(auto ref, NamedReference::Make(std::move(name)));
      return ref;
    }
    return JsonParseError("Invalid term format, expected string or object: {}",
                          SafeDumpJson(json));
  }

  return JsonParseError("Invalid term format, expected string or object: {}",
                        SafeDumpJson(json));
}

/// Parse a named reference from JSON
Result<std::unique_ptr<UnboundTransform>> UnboundTransformFromJson(
    const nlohmann::json& json) {
  if (json.is_object()) {
    ICEBERG_ASSIGN_OR_RAISE(auto typeStr, GetJsonValue<std::string>(json, kType));

    if (typeStr == kTransform) {
      ICEBERG_ASSIGN_OR_RAISE(auto transform_str,
                              GetJsonValue<std::string>(json, kTransform));
      ICEBERG_ASSIGN_OR_RAISE(auto term_name, GetJsonValue<std::string>(json, kTerm));
      ICEBERG_ASSIGN_OR_RAISE(auto transform, TransformFromString(transform_str));
      ICEBERG_ASSIGN_OR_RAISE(auto named_reference,
                              NamedReference::Make(std::move(term_name)));
      ICEBERG_ASSIGN_OR_RAISE(auto ref, UnboundTransform::Make(std::move(named_reference),
                                                               std::move(transform)));
      return ref;
    }
    return JsonParseError("Invalid term format, unexpected transform type {}",
                          SafeDumpJson(json));
  }

  return JsonParseError("Invalid term format, expected string or object: {}",
                        SafeDumpJson(json));
}

nlohmann::json NamedReferenceToJson(const NamedReference& ref) {
  return std::string(ref.name());
}

nlohmann::json UnboundTransformToJson(const UnboundTransform& transform) {
  nlohmann::json json;
  json[kType] = kTransform;
  json[kTransform] = transform.transform()->ToString();
  // Note: const_cast is safe here because reference() just returns a shared_ptr
  // and we're only reading from it. The method is not const due to interface design.
  auto& mutable_transform = const_cast<UnboundTransform&>(transform);
  json[kTerm] = std::string(mutable_transform.reference()->name());
  return json;
}

Result<std::shared_ptr<Expression>> ExpressionFromJson(const nlohmann::json& json) {
  // Handle boolean
  if (json.is_boolean()) {
    return json.get<bool>() ? std::static_pointer_cast<Expression>(True::Instance())
                            : std::static_pointer_cast<Expression>(False::Instance());
  }
  return JsonParseError("Only booleans are currently supported");
}

nlohmann::json ToJson(const Expression& expr) {
  switch (expr.op()) {
    case Expression::Operation::kTrue:
      return true;

    case Expression::Operation::kFalse:
      return false;
    default:
      throw std::logic_error("Only booleans are currently supported");
  }
}

#define ICEBERG_DEFINE_FROM_JSON(Model)                                        \
  template <>                                                                  \
  Result<std::shared_ptr<Model>> FromJson<Model>(const nlohmann::json& json) { \
    return Model##FromJson(json);                                              \
  }

ICEBERG_DEFINE_FROM_JSON(Expression)

}  // namespace iceberg
