#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace idc {

/// Shopping cart API controller (Redis-backed).
///
/// Cart data is stored in Redis as a hash under key `cart:{distributor_id}`.
/// Each field is a sequential item ID, the value is a JSON string of the
/// cart item: { product_id, quantity, period_months, config, ... }.
///
/// All endpoints require JWT authentication. Since carts are per-distributor
/// (not per-user), dealers can only access their own cart; admins can access
/// any distributor's cart via query param.
///
/// Endpoints:
///   POST   /api/v1/cart/items         — add product to cart
///   GET    /api/v1/cart               — list cart items with prices
///   PUT    /api/v1/cart/items/{id}    — update quantity / config
///   DELETE /api/v1/cart/items/{id}    — remove cart item
class CartController : public drogon::HttpController<CartController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(CartController::addItem,
                      "/api/v1/cart/items", drogon::Post,
                      "JWTFilter");
        ADD_METHOD_TO(CartController::listCart,
                      "/api/v1/cart", drogon::Get,
                      "JWTFilter");
        ADD_METHOD_TO(CartController::updateItem,
                      "/api/v1/cart/items/{id}", drogon::Put,
                      "JWTFilter");
        ADD_METHOD_TO(CartController::removeItem,
                      "/api/v1/cart/items/{id}", drogon::Delete,
                      "JWTFilter");
    METHOD_LIST_END

    /// POST /api/v1/cart/items
    /// Body: { product_id, quantity, period_months, config }
    static void addItem(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// GET /api/v1/cart
    /// Query: ?distributor_id= (admin only)
    static void listCart(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /// PUT /api/v1/cart/items/{id}
    /// Body: { quantity?, period_months?, config? }
    static void updateItem(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id);

    /// DELETE /api/v1/cart/items/{id}
    static void removeItem(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           const std::string& id);

private:
    /// Get the distributor_id for the current request.
    /// Admin can specify ?distributor_id= to view another's cart.
    /// Dealer always gets their own distributor_id.
    static int64_t resolveDistributorId(const drogon::HttpRequestPtr& req);

    /// Get the Redis cart key for a distributor.
    static std::string cartKey(int64_t distributorId) {
        return "cart:" + std::to_string(distributorId);
    }
};

} // namespace idc
