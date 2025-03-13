#include "fednlib.h"
#include "../../cnpy/cnpy.h"
#include <torch/torch.h>
#include <torch/script.h>
#include <vector>
#include <fstream>
//#include <armadillo>
#include <typeinfo>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>

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

// Save the model parameters to a binary file

void saveParameters(Net& model, const std::string& out_path) {

    std::cout << "Saving model to " << out_path << "..." << std::endl;

    try {
        // Convert tensors to raw float arrays
        float* fc1_weight = model.fc1->weight.data_ptr<float>();
        float* fc1_bias = model.fc1->bias.data_ptr<float>();

        float* fc2_weight = model.fc2->weight.data_ptr<float>();
        float* fc2_bias = model.fc2->bias.data_ptr<float>();

        float* fc3_weight = model.fc3->weight.data_ptr<float>();
        float* fc3_bias = model.fc3->bias.data_ptr<float>();

        // Save tensors to NPZ with specific names (without .npy suffix)
        cnpy::npz_save(out_path, "0", fc1_weight, {64, 784}, "w");  // Write mode for the first file
        cnpy::npz_save(out_path, "1", fc1_bias, {64}, "a");         // Append mode for the rest
        cnpy::npz_save(out_path, "2", fc2_weight, {32, 64}, "a");
        cnpy::npz_save(out_path, "3", fc2_bias, {32}, "a");
        cnpy::npz_save(out_path, "4", fc3_weight, {10, 32}, "a");
        cnpy::npz_save(out_path, "5", fc3_bias, {10}, "a");

        std::cout << "Model parameters saved successfully to " << out_path << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error saving NPZ file: " << e.what() << std::endl;
    }
}

// Load the model parameters from a binary file

Net loadParameters(const std::string& in_path) {

    std::cout << "Loading model parameters from " << in_path << "..." << std::endl;

    Net model;
   
    if (!std::filesystem::exists(in_path)) {
        std::cerr << "Error: model file " << in_path << " not found! " << std::endl;
        return model;    
    }
    else {
        std::cout << "Model file exist: " << in_path << "found! " << std::endl;	    

    }    

    // Step 1: Create temporary extraction folder
    std::string extract_dir = in_path + "_extracted";
    std::filesystem::create_directories(extract_dir);

    // Step 2: Extract the zip file
    std::string unzip_cmd = "unzip -o " + in_path + " -d " + extract_dir;
    int unzip_status = system(unzip_cmd.c_str());
    if (unzip_status != 0) {
        std::cerr << "Error: Failed to unzip file " << in_path << std::endl;
        return model;
    }

    // Step 3: Verify extracted files
    for (int i = 0; i < 6; i++) {
        std::string file_path = extract_dir + "/" + std::to_string(i) + ".npy";
        if (!std::filesystem::exists(file_path)) {
            std::cerr << "Error: Expected file " << file_path << " not found!" << std::endl;
            return model;
        }
    }

    // Step 4: Load model weights
    try {
        cnpy::NpyArray fc1_weight_arr = cnpy::npy_load(extract_dir + "/0.npy");
        cnpy::NpyArray fc1_bias_arr = cnpy::npy_load(extract_dir + "/1.npy");
        cnpy::NpyArray fc2_weight_arr = cnpy::npy_load(extract_dir + "/2.npy");
        cnpy::NpyArray fc2_bias_arr = cnpy::npy_load(extract_dir + "/3.npy");
        cnpy::NpyArray fc3_weight_arr = cnpy::npy_load(extract_dir + "/4.npy");
        cnpy::NpyArray fc3_bias_arr = cnpy::npy_load(extract_dir + "/5.npy");

         // 🛠 **FIXED: Transpose the weight matrices**
        auto fc1_weight = torch::from_blob(fc1_weight_arr.data<float>(), {784, 64}).t().clone();
        auto fc1_bias = torch::from_blob(fc1_bias_arr.data<float>(), {64}).clone();
        auto fc2_weight = torch::from_blob(fc2_weight_arr.data<float>(), {64, 32}).t().clone();
        auto fc2_bias = torch::from_blob(fc2_bias_arr.data<float>(), {32}).clone();
        auto fc3_weight = torch::from_blob(fc3_weight_arr.data<float>(), {32, 10}).t().clone();
        auto fc3_bias = torch::from_blob(fc3_bias_arr.data<float>(), {10}).clone();


	// Correctly updating model weights
        model.fc1->weight.set_data(fc1_weight);
        model.fc1->bias.set_data(fc1_bias);
        model.fc2->weight.set_data(fc2_weight);
        model.fc2->bias.set_data(fc2_bias);
        model.fc3->weight.set_data(fc3_weight);
        model.fc3->bias.set_data(fc3_bias);

        std::cout << "Model parameters loaded successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading model parameters: " << e.what() << std::endl;
    }

    // Step 5: Cleanup extracted directory
    std::filesystem::remove_all(extract_dir);
    std::cout << "Cleanup complete: Removed " << extract_dir << std::endl;

    return model;
}

// Function to compute accuracy
float compute_accuracy(torch::Tensor predictions, torch::Tensor labels) {
    auto predicted_labels = predictions.argmax(1);
    return predicted_labels.eq(labels).sum().item<float>() / labels.size(0);
}
// Callback function for writing downloaded data to a file
size_t writeDownloadedDataToFile(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    file->write(static_cast<char*>(contents), realsize);
    return realsize;
}

void downloadMNISTData(const std::string& data_path) {
    std::cout << "Downloading MNIST data..." << std::endl;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return;
    }

    std::string base_url = "https://storage.googleapis.com/public-scaleout/mnist-pytorch/data/train/MNIST/raw/";
    std::string file_names[4] = {
        "train-images-idx3-ubyte",
        "train-labels-idx1-ubyte",
        "t10k-images-idx3-ubyte",
        "t10k-labels-idx1-ubyte"
    };

    for (const auto& file_name : file_names) {
        std::string file_path = data_path + "/" + file_name;
        std::cout << "Downloading " << file_name << " from " << base_url << std::endl;
        
        std::ofstream output_file(file_path, std::ios::binary);
        if (!output_file.is_open()) {
            std::cerr << "Failed to open file for writing: " << file_path << std::endl;
            continue;
        }

        std::cout << "Opened file for " << file_name << std::endl;

        curl_easy_setopt(curl, CURLOPT_URL, (base_url + file_name).c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeDownloadedDataToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_file);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  // Enable verbose output for debugging

        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            std::cerr << "Download failed for " << file_name << ": " << curl_easy_strerror(res) << std::endl;
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                std::cout << "Download successful for " << file_name << std::endl;
                
                // Check file size
                output_file.seekp(0, std::ios::end);
                std::streampos fileSize = output_file.tellp();
                std::cout << "Downloaded file size: " << fileSize << " bytes" << std::endl;
            } else {
                std::cerr << "HTTP error: " << http_code << std::endl;
            }
        }

        output_file.close();
        std::cout << "Closed file for " << file_name << std::endl;
    }

    curl_easy_cleanup(curl);
}

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}

    void train(const std::string& inModelPath, const std::string& outModelPath) override {
        std::cout << "USER-DEFINED CODE: Training model..." << std::endl;
        
        Net model = loadParameters(inModelPath);

        model.train();

        // Set the hyperparameters
        const int num_epochs = 1;
        const int batch_size = 32;
        const double learning_rate = 0.001;

        // Load the MNIST dataset
        std::string data_path = "../data";
        auto train_dataset = torch::data::datasets::MNIST(data_path)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto test_dataset = torch::data::datasets::MNIST(data_path, torch::data::datasets::MNIST::Mode::kTest)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto train_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(train_dataset), batch_size);
        auto test_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
            std::move(test_dataset), batch_size);

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
    }
    void validate(const std::string& inModelPath, const std::string& outMetricPath) override {

        std::cout << "USER-DEFINED CODE: Validating model..." << std::endl;

        Net model = loadParameters(inModelPath);

        model.eval();  // Set the model to evaluation mode

        // Load the train and test datasets
        std::string data_path = "../data";
        auto train_dataset = torch::data::datasets::MNIST(data_path)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        auto test_dataset = torch::data::datasets::MNIST(data_path, torch::data::datasets::MNIST::Mode::kTest)
                                .map(torch::data::transforms::Normalize<>(0.1307, 0.3081))
                                .map(torch::data::transforms::Stack<>());

        // Set the batch size and create DataLoader for train and test sets
        const int64_t batch_size = 32;
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

        saveMetricsToFile(metrics, outMetricPath);
    }
};

int main(int argc, char** argv) {
    FednClient client("../../../client.yaml");

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    // Check if ../data exists
    std::string data_path = "../data";
    if (!std::filesystem::exists(data_path)) {
        // Create the ../data directory
        std::filesystem::create_directory(data_path);

        // Download the MNIST dataset
        downloadMNISTData(data_path);
    }
    
    std::string seed_model_path = "../seed.npz";
    if (!std::filesystem::exists(seed_model_path)) {
        // Create the ../data directory
        Net model;
        saveParameters(model, seed_model_path);
    }

    client.run(customGrpcClient);

    return 0;
}
