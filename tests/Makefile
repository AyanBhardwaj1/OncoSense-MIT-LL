CXX ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O1
FW = ../firmware/OncoSense

test: test_skin_model test_ai
	./test_skin_model
	./test_ai

test_skin_model: test_skin_model.cpp $(FW)/SkinModel.cpp $(FW)/SkinModel.h
	$(CXX) $(CXXFLAGS) -o $@ test_skin_model.cpp $(FW)/SkinModel.cpp

test_ai: test_ai.cpp nn_test_vectors.h $(FW)/Features.cpp $(FW)/NeuralNet.cpp $(FW)/Novelty.cpp $(FW)/model_weights.h
	$(CXX) $(CXXFLAGS) -o $@ test_ai.cpp $(FW)/Features.cpp $(FW)/NeuralNet.cpp $(FW)/Novelty.cpp

clean:
	rm -f test_skin_model test_ai

.PHONY: test clean
