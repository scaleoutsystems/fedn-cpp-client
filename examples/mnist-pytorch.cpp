#include "../include/fednlib.h"
#include "../../cnpy/cnpy.h"
#include <torch/torch.h>
#include <vector>
#include <fstream>
#include <armadillo>
#include <typeinfo>

// Define the model class
struct Net : torch::nn::Module {
    torch::nn::Linear fc1{nullptr}, fc2{nullptr}, fc3{nullptr};

    Net() {
        fc1 = register_module("fc1", torch::nn::Linear(784, 64));
        fc2 = register_module("fc2", torch::nn::Linear(64, 32));
        fc3 = register_module("fc3", torch::nn::Linear(32, 10));
    }

    // Forward pass
    torch::Tensor forward(torch::Tensor x) {
        x = torch::relu(fc1->forward(x.view({x.size(0), 784})));
        x = torch::dropout(x, 0.5, is_training());
        x = torch::relu(fc2->forward(x));
        x = torch::log_softmax(fc3->forward(x), 1);
        return x;
    }
};

Net armadilloToTorch(arma::mat loadedWeights) {
    // Create the model
    Net model;

    // Get the weights from the Armadillo matrix
    double* weight_data = loadedWeights.memptr();  // Get pointer to the raw data

    // Assuming loadedWeights contains the entire vector of weights, split it back

    // 1. Assign weights for fc1
    int fc1_weight_size = 784 * 64;
    torch::Tensor fc1_weight = torch::from_blob(weight_data, {64, 784}, torch::kFloat64).clone();
    model.fc1->weight.data().copy_(fc1_weight);
    weight_data += fc1_weight_size;  // Move pointer to the next block of weights

    // 2. Assign bias for fc1
    int fc1_bias_size = 64;
    torch::Tensor fc1_bias = torch::from_blob(weight_data, {64}, torch::kFloat64).clone();
    model.fc1->bias.data().copy_(fc1_bias);
    weight_data += fc1_bias_size;

    // 3. Assign weights for fc2
    int fc2_weight_size = 64 * 32;
    torch::Tensor fc2_weight = torch::from_blob(weight_data, {32, 64}, torch::kFloat64).clone();
    model.fc2->weight.data().copy_(fc2_weight);
    weight_data += fc2_weight_size;

    // 4. Assign bias for fc2
    int fc2_bias_size = 32;
    torch::Tensor fc2_bias = torch::from_blob(weight_data, {32}, torch::kFloat64).clone();
    model.fc2->bias.data().copy_(fc2_bias);
    weight_data += fc2_bias_size;

    // 5. Assign weights for fc3
    int fc3_weight_size = 32 * 10;
    torch::Tensor fc3_weight = torch::from_blob(weight_data, {10, 32}, torch::kFloat64).clone();
    model.fc3->weight.data().copy_(fc3_weight);
    weight_data += fc3_weight_size;

    // 6. Assign bias for fc3
    int fc3_bias_size = 10;
    torch::Tensor fc3_bias = torch::from_blob(weight_data, {10}, torch::kFloat64).clone();
    model.fc3->bias.data().copy_(fc3_bias);

    return model;
}

// Save the model parameters to a binary file
void saveParameters(Net& model, const std::string& out_path) {
    std::vector<float> parameters_vec;

    // Iterate over all parameters in the model
    for (const auto& param : model.parameters()) {
        // Get the parameter's tensor
        torch::Tensor flattened_param = param.view(-1);  // Flatten the tensor

        // Convert the tensor to a contiguous array and append to the vector
        parameters_vec.insert(parameters_vec.end(),
                              flattened_param.data_ptr<float>(),  // Get raw pointer
                              flattened_param.data_ptr<float>() + flattened_param.numel());  // Get all elements
    }

    // Print the size of the concatenated parameters (for debugging)
    std::cout << "Concatenated parameters size: " << parameters_vec.size() << std::endl;

    // Save the parameters to a binary file
    std::ofstream output_file(out_path, std::ios::out | std::ios::binary);
    output_file.write(reinterpret_cast<const char*>(parameters_vec.data()), parameters_vec.size() * sizeof(float));
    output_file.close();

    std::cout << "Model parameters saved to: " << out_path << std::endl;
}

// Load the model parameters from a binary file
void loadParameters(Net& model, const std::string& in_path) {
    // Read the parameters from the binary file
    std::ifstream input_file(in_path, std::ios::in | std::ios::binary);
    std::vector<float> parameters_vec((std::istreambuf_iterator<char>(input_file)),
                                      std::istreambuf_iterator<char>());
    input_file.close();

    // Print the size of the loaded parameters (for debugging)
    std::cout << "Loaded parameters size: " << parameters_vec.size() << std::endl;

    // Pointer to the raw data
    float* param_data = parameters_vec.data();

    // Iterate over all parameters in the model and load the data
    for (auto& param : model.parameters()) {
        // Get the parameter's tensor
        torch::Tensor flattened_param = param.view(-1);  // Flatten the tensor

        // Copy the data from the vector to the tensor
        std::memcpy(flattened_param.data_ptr<float>(), param_data, flattened_param.numel() * sizeof(float));

        // Move the pointer to the next block of data
        param_data += flattened_param.numel();
    }

    std::cout << "Model parameters loaded from: " << in_path << std::endl;
}

// Function to compute accuracy
float compute_accuracy(torch::Tensor predictions, torch::Tensor labels) {
    auto predicted_labels = predictions.argmax(1);
    return predicted_labels.eq(labels).sum().item<float>() / labels.size(0);
}

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}

    void train(const std::string& inModelPath, const std::string& outModelPath) override {
        std::cout << "USER-DEFINED CODE: Training model..." << std::endl;
        
        // arma::mat loadedData;
        // loadedData.load(inModelPath, arma::raw_binary);
        // Convert the Armadillo matrix to a PyTorch model
        // Net model = armadilloToTorch(loadedData);
        
        Net model;
        // loadParameters(model, inModelPath);

        // Set the hyperparameters
        const int num_epochs = 20;
        const int batch_size = 32;
        const double learning_rate = 0.001;

        // Load the MNIST dataset
        auto train_dataset = torch::data::datasets::MNIST("../mnist")
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto train_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(train_dataset), batch_size);

        // Set the model to the device
        torch::Device device(torch::cuda::is_available() ? torch::kCUDA : torch::kCPU);
        model.to(device);

        // Define optimizer
        torch::optim::SGD optimizer(model.parameters(), torch::optim::SGDOptions(learning_rate));

        namespace F = torch::nn::functional;

        // Training loop
        for (int epoch = 1; epoch <= num_epochs; ++epoch) {
            size_t batch_idx = 0;
            for (auto& batch : *train_loader) {
                auto data = batch.data.to(device);
                auto target = batch.target.to(device);

                optimizer.zero_grad();
                auto output = model.forward(data);
                auto loss = F::nll_loss(output, target);
                loss.backward();
                optimizer.step();

                if (batch_idx++ % 100 == 0) {
                    std::cout << "Train Epoch: " << epoch
                            << " [" << batch_idx * batch.data.size(0) << "/60000]"
                            << " Loss: " << loss.item<double>() << std::endl;
                }
            }
        }

        // Save the updated model parameters to a binary file
        saveParameters(model, outModelPath);

        // OLD CODE
        // std::string modelData = loadModelFromFile(inModelPath);
        // std::string modelUpdateData = modelData;
        // saveModelToFile(modelUpdateData, outModelPath);
    }
    void validate(const std::string& inModelPath, const std::string& outMetricPath) override {

        std::cout << "USER-DEFINED CODE: Validating model..." << std::endl;

        arma::mat loadedData;
        loadedData.load(inModelPath, arma::raw_binary);
        Net model = armadilloToTorch(loadedData);

        model.eval();  // Set the model to evaluation mode

        // Load the train and test datasets
        auto train_dataset = torch::data::datasets::MNIST("../mnist")
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto test_dataset = torch::data::datasets::MNIST("../mnist", torch::data::datasets::MNIST::Mode::kTest)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        // Set the batch size and create DataLoader for train and test sets
        const int64_t batch_size = 64;
        auto train_loader = torch::data::make_data_loader(std::move(train_dataset), batch_size);
        auto test_loader = torch::data::make_data_loader(std::move(test_dataset), batch_size);

        // Set the device (use GPU if available)
        torch::Device device(torch::cuda::is_available() ? torch::kCUDA : torch::kCPU);
        model.to(device);

        // Define loss function
        torch::nn::NLLLoss criterion;

        // Track metrics
        float train_loss = 0.0, test_loss = 0.0;
        float train_accuracy = 0.0, test_accuracy = 0.0;

        // Evaluate the model on the training set
        int total_train_samples = 0;
        for (const auto& batch : *train_loader) {
            auto data = batch.data.to(device);
            auto labels = batch.target.to(device);

            // Forward pass
            auto output = model.forward(data);
            auto loss = criterion(output, labels);

            // Accumulate loss and accuracy
            train_loss += loss.item<float>() * data.size(0);
            train_accuracy += compute_accuracy(output, labels) * data.size(0);
            total_train_samples += data.size(0);
        }

        // Compute average loss and accuracy
        train_loss /= total_train_samples;
        train_accuracy /= total_train_samples;

        // Evaluate the model on the test set
        int total_test_samples = 0;
        for (const auto& batch : *test_loader) {
            auto data = batch.data.to(device);
            auto labels = batch.target.to(device);

            // Forward pass
            auto output = model.forward(data);
            auto loss = criterion(output, labels);

            // Accumulate loss and accuracy
            test_loss += loss.item<float>() * data.size(0);
            test_accuracy += compute_accuracy(output, labels) * data.size(0);
            total_test_samples += data.size(0);
        }

        // Compute average loss and accuracy for test set
        test_loss /= total_test_samples;
        test_accuracy /= total_test_samples;

        // Report the metrics in JSON format
        json metrics;
        metrics["training_loss"] = train_loss;
        metrics["training_accuracy"] = train_accuracy;
        metrics["test_loss"] = test_loss;
        metrics["test_accuracy"] = test_accuracy;

        // Print the metrics
        std::cout << "Metrics: " << metrics.dump(4) << std::endl;

        saveMetricsToFile(metrics, outMetricPath);
    }
};

int main(int argc, char** argv) {
    FednClient client("../../client.yaml");

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    client.run(customGrpcClient);

    return 0;
}