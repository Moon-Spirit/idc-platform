#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Product catalog API controller.
///
/// All endpoints are protected by JWTFilter.
/// Mutation endpoints additionally require RBACFilter with appropriate
/// permissions (product:create, product:update, product:delete).
///
/// Endpoints:
///   GET    /api/v1/products           — list products (type filter & keyword search)
///   GET    /api/v1/products/types     — product type enum list
///   GET    /api/v1/products/:id       — product detail
///   POST   /api/v1/products           — create product (admin: product:create)
///   PUT    /api/v1/products/:id       — update product (admin: product:update)
///   DELETE /api/v1/products/:id       — soft delete (admin: product:delete)
///   GET    /api/v1/products/:id/price — effective price for current user
class ProductController : public drogon::HttpController<ProductController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(ProductController::listProducts,
                      "/api/v1/products", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(ProductController::getProductTypes,
                      "/api/v1/products/types", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(ProductController::getProduct,
                      "/api/v1/products/{id}", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(ProductController::createProduct,
                      "/api/v1/products", drogon::Post,
                      "JWTFilter", "RBACFilter(product:create)");
        ADD_METHOD_TO(ProductController::updateProduct,
                      "/api/v1/products/{id}", drogon::Put,
                      "JWTFilter", "RBACFilter(product:update)");
        ADD_METHOD_TO(ProductController::deleteProduct,
                      "/api/v1/products/{id}", drogon::Delete,
                      "JWTFilter", "RBACFilter(product:delete)");
        ADD_METHOD_TO(ProductController::getProductPrice,
                      "/api/v1/products/{id}/price", drogon::Get,
                      "JWTFilter");
    METHOD_LIST_END

    // ── Handlers (static) ──────────────────────────────────────────────────

    static void listProducts(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void getProductTypes(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void getProduct(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           int64_t id);

    static void createProduct(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void updateProduct(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              int64_t id);

    static void deleteProduct(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              int64_t id);

    static void getProductPrice(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                int64_t id);
};

} // namespace idc
