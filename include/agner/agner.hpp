#pragma once

/**
 * @file agner.hpp
 * @brief Agner - A C++20 header-only actor framework.
 *
 * Agner provides lightweight actors with message passing, supervision trees,
 * and coroutine-based scheduling inspired by Erlang/OTP.
 *
 * @section usage Basic Usage
 * @code
 * #include <agner/agner.hpp>
 * using namespace agner;
 *
 * struct Ping {};
 * struct Pong {};
 *
 * class PongActor : public Actor<Scheduler, PongActor, Messages<Ping>> {
 *  public:
 *   using Actor::Actor;
 *   task<void> run() {
 *     co_await receive([&](Ping&) { send(sender_, Pong{}); });
 *   }
 *   ActorRef sender_;
 * };
 *
 * class PingActor : public Actor<Scheduler, PingActor, Messages<Pong>> {
 *  public:
 *   using Actor::Actor;
 *   task<void> run() {
 *     auto pong = spawn<PongActor>();
 *     send(pong, Ping{});
 *     co_await receive([](Pong&) { /* received reply */ });
 *   }
 * };
 *
 * int main() {
 *   Scheduler sched;
 *   sched.spawn<PingActor>();
 *   sched.run();
 * }
 * @endcode
 */

#include "agner/actor.hpp"
#include "agner/actor_concepts.hpp"
#include "agner/actor_control.hpp"
#include "agner/errors.hpp"
#include "agner/scheduler.hpp"
#include "agner/scheduler_base.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/supervisor.hpp"
#include "agner/task.hpp"
