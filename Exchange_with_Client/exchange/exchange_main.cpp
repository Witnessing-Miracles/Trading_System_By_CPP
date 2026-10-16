#include <csignal>

#include "matcher/matching_engine.h"
#include "market_data/market_data_publisher.h"
#include "order_server/order_server.h"

Common::Logger *logger = nullptr;
Exchange::MatchingEngine *matching_engine = nullptr;
Exchange::MarketDataPublisher *market_data_publisher = nullptr;
Exchange::OrderServer *order_server = nullptr;

// 信号处理函数只设置退出标志，不在信号上下文中 sleep、delete 或 exit。
volatile std::sig_atomic_t stop_requested = 0;

void signal_handler(int) {
  stop_requested = 1;
}

int main(int, char **) {
  logger = new Common::Logger("exchange_main.log");

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // usleep 的单位是微秒：100,000 微秒 = 0.1 秒。
  const int sleep_time = 100 * 1000;

  // Lock-free queues:
  // order server -> matching engine -> market data publisher.
  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);

  std::string time_str;

  logger->log("%:% %() % Starting Matching Engine...\n",
              __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str));
  matching_engine =
      new Exchange::MatchingEngine(&client_requests, &client_responses, &market_updates);
  matching_engine->start();

  const std::string mkt_pub_iface = "ens37";
  const std::string snap_pub_ip = "233.252.14.1";
  const std::string inc_pub_ip = "233.252.14.3";
  const int snap_pub_port = 20000;
  const int inc_pub_port = 20001;

  logger->log("%:% %() % Starting Market Data Publisher...\n",
              __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str));
  market_data_publisher = new Exchange::MarketDataPublisher(
      &market_updates, mkt_pub_iface, snap_pub_ip, snap_pub_port,
      inc_pub_ip, inc_pub_port);
  market_data_publisher->start();

  const std::string order_gw_iface = "ens37";
  const int order_gw_port = 12345;

  logger->log("%:% %() % Starting Order Server...\n",
              __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str));
  order_server =
      new Exchange::OrderServer(&client_requests, &client_responses,
                                order_gw_iface, order_gw_port);
  order_server->start();

  // 保持主线程响应 Ctrl+C；不再每 0.1 秒写一条日志。
  while (!stop_requested) {
    usleep(sleep_time);
  }

  logger->log("%:% %() % Shutdown requested; stopping components.\n",
              __FILE__, __LINE__, __FUNCTION__,
              Common::getCurrentTimeStr(&time_str));

  // 先停订单入口，再停撮合引擎，最后停行情发布器。
  order_server->stop();
  delete order_server;
  order_server = nullptr;

  matching_engine->stop();
  delete matching_engine;
  matching_engine = nullptr;

  market_data_publisher->stop();
  delete market_data_publisher;
  market_data_publisher = nullptr;

  delete logger;
  logger = nullptr;

  return 0;
}
