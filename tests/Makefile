CXX ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1

test: test_skin_model
	./test_skin_model

test_skin_model: test_skin_model.cpp ../firmware/OncoSense/SkinModel.cpp ../firmware/OncoSense/SkinModel.h
	$(CXX) $(CXXFLAGS) -o $@ test_skin_model.cpp ../firmware/OncoSense/SkinModel.cpp

clean:
	rm -f test_skin_model

.PHONY: test clean
