#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <google/protobuf/util/message_differencer.h>

#include "tradebot/v1/person.grpc.pb.h"

namespace {
class PeopleGreeterImpl final : public EXAMPLE::PersonAger::Service {
 public:
  ::grpc::Status PersonAger(::grpc::ServerContext* context,
                            const ::EXAMPLE::PersonAgeRequest* request,
                            ::EXAMPLE::PersonAgeResponse* response) override {
    EXAMPLE::Person person = request->person();

    int age = person.age();
    int toAdd = request->age_to_add();
    int result = age + toAdd;

    person.set_age(result);

    response->set_age_added(toAdd);
    *response->mutable_person() = person;

    return ::grpc::Status::OK;
  }
};

TEST(ProtobuffTest, BasicAssertion) {
  EXAMPLE::Person person;

  person.set_name("John");

  EXPECT_EQ("John", person.name());
}

TEST(ProtobuffTest, GreetAndReply) {
  ::grpc::ServerContext context;

  EXAMPLE::Person person;
  EXAMPLE::PersonAgeRequest req;

  *req.mutable_person() = person;
  req.set_age_to_add(5);

  EXAMPLE::PersonAgeResponse response;

  PeopleGreeterImpl peopleGreeter;
  ::grpc::Status status = peopleGreeter.PersonAger(&context, &req, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(req.person().age() + req.age_to_add(), response.person().age());
}

TEST(ProtobuffTest, testSerialising) {
    EXAMPLE::Person p;
    EXAMPLE::PersonAgeRequest req;

    p.set_age(5);
    p.set_name("John");
    *req.mutable_person() = p;
    req.set_age_to_add(5);

    std::string bytes;
    if(!req.SerializeToString(&bytes)) {
        std::cout << "Error Serializing" << std::endl;
    }

    EXAMPLE::PersonAgeRequest result;
    if (!result.ParseFromString(bytes)) {
        std::cout << "Error Parsing" << std::endl;
    }

    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(req, result));
}
}  // namespace