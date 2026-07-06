#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Price template API controller.
///
/// All endpoints protected by JWTFilter. Mutations additionally require
/// RBACFilter with price:create / price:update / price:delete permissions.
///
/// Endpoints:
///   GET    /api/v1/price-templates               — list templates
///   POST   /api/v1/price-templates               — create template
///   GET    /api/v1/price-templates/:id            — template detail
///   PUT    /api/v1/price-templates/:id            — update template
///   DELETE /api/v1/price-templates/:id            — soft delete
///   GET    /api/v1/price-templates/:id/products   — product prices in template
///   PUT    /api/v1/price-templates/:id/products/:product_id  — set product price
class PriceTemplateController
    : public drogon::HttpController<PriceTemplateController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(PriceTemplateController::listTemplates,
                      "/api/v1/price-templates", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(PriceTemplateController::createTemplate,
                      "/api/v1/price-templates", drogon::Post,
                      "JWTFilter", "RBACFilter(price:create)");
        ADD_METHOD_TO(PriceTemplateController::getTemplate,
                      "/api/v1/price-templates/{id}", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(PriceTemplateController::updateTemplate,
                      "/api/v1/price-templates/{id}", drogon::Put,
                      "JWTFilter", "RBACFilter(price:update)");
        ADD_METHOD_TO(PriceTemplateController::deleteTemplate,
                      "/api/v1/price-templates/{id}", drogon::Delete,
                      "JWTFilter", "RBACFilter(price:delete)");
        ADD_METHOD_TO(PriceTemplateController::getTemplateProducts,
                      "/api/v1/price-templates/{id}/products", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(PriceTemplateController::setProductPrice,
                      "/api/v1/price-templates/{id}/products/{product_id}",
                      drogon::Put,
                      "JWTFilter", "RBACFilter(price:update)");
    METHOD_LIST_END

    static void listTemplates(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void createTemplate(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    static void getTemplate(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            int64_t id);

    static void updateTemplate(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               int64_t id);

    static void deleteTemplate(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                               int64_t id);

    static void getTemplateProducts(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id);

    static void setProductPrice(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                int64_t id, int64_t productId);
};

} // namespace idc
