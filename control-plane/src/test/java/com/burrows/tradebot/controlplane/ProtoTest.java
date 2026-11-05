package com.burrows.tradebot.controlplane;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.Test;
import io.grpc.stub.StreamObserver;

import com.burrows.tradebot.controlplane.protobuf.*;
import com.google.protobuf.InvalidProtocolBufferException;

public class ProtoTest {
    private static class PeopleGreeterImpl extends PersonAgerGrpc.PersonAgerImplBase {
        @Override
        public void personAger(PersonAgeRequest request,
                StreamObserver<PersonAgeResponse> responseObserver) {

            Person person = request.getPerson().toBuilder()
                    .setAge(request.getPerson().getAge() + request.getAgeToAdd())
                    .build();

            PersonAgeResponse response = PersonAgeResponse.newBuilder()
                    .setPerson(person)
                    .setAgeAdded(request.getAgeToAdd())
                    .build();

            responseObserver.onNext(response);

            responseObserver.onCompleted();
        }
    }

    @Test
    public void basicAssertion() {
        Person p = Person.newBuilder().setName("John").build();

        assertEquals(p.getName(), "John");

    };

    @Test
    public void ageIsAddedCorrectly() {
        List<PersonAgeResponse> responses = new ArrayList<>();
        boolean[] completed = { false };

        Person p = Person.newBuilder().setName("John").setAge(5).build();
        PersonAgeRequest request = PersonAgeRequest.newBuilder().setPerson(p).setAgeToAdd(5).build();

        PeopleGreeterImpl greeter = new PeopleGreeterImpl();

        StreamObserver<PersonAgeResponse> responseObserver = new StreamObserver<PersonAgeResponse>() {
            @Override
            public void onNext(PersonAgeResponse response) {
                responses.add(response);
            }

            @Override
            public void onError(Throwable t) {
                // can't be bothered to handle this properly as is only a test :)
                throw new RuntimeException("BROKE :(");
            }

            @Override
            public void onCompleted() {
                completed[0] = true;
            }
        };

        greeter.personAger(request, responseObserver);

        assertEquals(p.getName(), "John");
        assertEquals(10, responses.get(0).getPerson().getAge());
        assertEquals(responses.size(), 1);
        assertTrue(completed[0]);

    };

    @Test
    public void testSerialisation() throws InvalidProtocolBufferException {
        Person p = Person.newBuilder().setName("John").setAge(2).build();
        PersonAgeRequest request = PersonAgeRequest.newBuilder()
                .setPerson(p).setAgeToAdd(5).build();

        byte[] arr = request.toByteArray();
        PersonAgeRequest result = PersonAgeRequest.parseFrom(arr);
        
        assertEquals(request, result);

    };

}