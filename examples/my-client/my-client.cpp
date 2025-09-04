#include "fednlib.h"
#include "fednlib.h"
#include "fednlib/ClientOptions.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <cnpy.h>              
#include <random>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <stdexcept>


#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>


#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fstream>


// returns path to a unique temp dir with the extracted files
static std::string unzip_to_temp(const std::string& npz_path) {
    // unique-ish dir: <file>.unz-<pid>-<rand>
    std::string dir = npz_path + ".unz-" + std::to_string(::getpid()) + "-" + std::to_string(std::rand());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string cmd = "unzip -o -qq \"" + npz_path + "\" -d \"" + dir + "\"";
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("unzip failed for: " + npz_path);
    }
    return dir;
}

static std::string pick_arr(const std::string& dir, int idx) {
    const std::string a = dir + "/arr_" + std::to_string(idx) + ".npy"; // NumPy's savez(_compressed)
    const std::string b = dir + "/" + std::to_string(idx) + ".npy";     // cnpy::npz_save("0", …)
    if (std::filesystem::exists(a)) return a;
    if (std::filesystem::exists(b)) return b;
    throw std::runtime_error("missing array file for index " + std::to_string(idx) +
                             " (looked for " + a + " or " + b + ")");
}

// Your minimal MLP param struct
struct MLPParams {
    int in = 0, hidden = 0, out = 0;
    std::vector<double> W0, W1, b0, b1;  // float64 to match NumPy/scikit-learn defaults
};

static MLPParams load_npz_mlp_via_unzip(const std::string& npz_path) {
    const std::string dir = unzip_to_temp(npz_path);

    cnpy::NpyArray A0 = cnpy::npy_load(pick_arr(dir, 0)); // W0 shape: (in, hidden)
    cnpy::NpyArray A1 = cnpy::npy_load(pick_arr(dir, 1)); // W1 shape: (hidden, out)
    cnpy::NpyArray A2 = cnpy::npy_load(pick_arr(dir, 2)); // b0 shape: (hidden,)
    cnpy::NpyArray A3 = cnpy::npy_load(pick_arr(dir, 3)); // b1 shape: (out,)

    MLPParams p;
    p.in     = static_cast<int>(A0.shape[0]);
    p.hidden = static_cast<int>(A0.shape[1]);
    p.out    = static_cast<int>(A1.shape[1]);

    p.W0.assign(A0.data<double>(), A0.data<double>() + A0.num_vals);
    p.W1.assign(A1.data<double>(), A1.data<double>() + A1.num_vals);
    p.b0.assign(A2.data<double>(), A2.data<double>() + A2.num_vals);
    p.b1.assign(A3.data<double>(), A3.data<double>() + A3.num_vals);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec); // clean up temp extraction
    return p;
}

static void save_npz_mlp(const std::string& path, const MLPParams& p) {
    // order matches Python: coefs_ + intercepts_  -> W0, W1, b0, b1
    cnpy::npz_save(path, "0", p.W0.data(), { (size_t)p.in, (size_t)p.hidden }, "w");
    cnpy::npz_save(path, "1", p.W1.data(), { (size_t)p.hidden, (size_t)p.out }, "a");
    cnpy::npz_save(path, "2", p.b0.data(), { (size_t)p.hidden }, "a");
    cnpy::npz_save(path, "3", p.b1.data(), { (size_t)p.out }, "a");
}

static MLPParams make_seed(int in, int hidden, int out, unsigned seed = 42) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> N(0.0, 0.05);

    MLPParams p; p.in = in; p.hidden = hidden; p.out = out;
    p.W0.resize(in * hidden);  for (auto& v : p.W0) v = N(rng);
    p.W1.resize(hidden * out); for (auto& v : p.W1) v = N(rng);
    p.b0.assign(hidden, 0.0);
    p.b1.assign(out, 0.0);
    return p;
}

// ---------- tiny dataset ----------
struct Dataset {
    std::vector<double> X; // row-major [n x d]
    std::vector<int>    y; // [n]
    size_t n=0, d=0;
};

static Dataset make_classification_like(size_t n, size_t d, unsigned seed=1234) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> N(0.0, 1.0);

    // two Gaussian blobs, shifted means
    std::vector<double> mu0(d, -1.0), mu1(d, +1.0);

    Dataset ds; ds.n = n; ds.d = d;
    ds.X.resize(n*d);
    ds.y.resize(n);

    for (size_t i=0;i<n;i++) {
        const bool cls = (i < n/2) ? 0 : 1; // half/half
        ds.y[i] = cls ? 1 : 0;
        const auto& mu = cls ? mu1 : mu0;
        for (size_t j=0;j<d;j++) {
            ds.X[i*d + j] = mu[j] + N(rng);
        }
    }
    // shuffle rows
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::shuffle(idx.begin(), idx.end(), rng);

    std::vector<double> Xshuf(n*d);
    std::vector<int>    yshuf(n);
    for (size_t i=0;i<n;i++) {
        const size_t k = idx[i];
        std::copy(ds.X.begin()+k*d, ds.X.begin()+(k+1)*d, Xshuf.begin()+i*d);
        yshuf[i] = ds.y[k];
    }
    ds.X.swap(Xshuf);
    ds.y.swap(yshuf);
    return ds;
}

// ---------- math helpers ----------
static inline double relu(double x){ return x>0? x:0; }
static inline double relu_grad(double x){ return x>0? 1.0:0.0; }

static void softmax_stable(std::vector<double>& z) {
    double m = *std::max_element(z.begin(), z.end());
    double sum = 0.0;
    for (double& v : z) { v = std::exp(v - m); sum += v; }
    for (double& v : z) v /= sum;
}

struct ForwardCache {
    std::vector<double> h;   // hidden activations size=hidden
    std::vector<double> z;   // logits size=out
    std::vector<double> p;   // probs size=out
};

// x[in] -> h[hidden] -> z[out] -> p[out]
static ForwardCache forward_one(const MLPParams& p, const double* x) {
    ForwardCache c;
    c.h.resize(p.hidden); c.z.resize(p.out); c.p.resize(p.out);

    // h = ReLU(x W0 + b0)
    for (int j=0;j<p.hidden;j++) {
        double s = p.b0[j];
        const int base = j; // W0 is [in x hidden], row-major as stored
        for (int i=0;i<p.in;i++) {
            s += x[i] * p.W0[i*p.hidden + j];
        }
        c.h[j] = relu(s);
    }
    // z = h W1 + b1
    for (int k=0;k<p.out;k++) {
        double s = p.b1[k];
        for (int j=0;j<p.hidden;j++) {
            s += c.h[j] * p.W1[j*p.out + k];
        }
        c.z[k] = s;
    }
    c.p = c.z;
    softmax_stable(c.p);
    return c;
}

// Cross-entropy loss for target class t
static inline double xent(const std::vector<double>& p, int t) {
    const double eps = 1e-12;
    return -std::log(std::max(p[t], eps));
}

struct Grads {
    std::vector<double> dW0, dW1, db0, db1;
    Grads(int in, int hidden, int out)
    : dW0(in*hidden,0.0), dW1(hidden*out,0.0), db0(hidden,0.0), db1(out,0.0) {}
    void zero(){ std::fill(dW0.begin(),dW0.end(),0.0); std::fill(dW1.begin(),dW1.end(),0.0);
                 std::fill(db0.begin(),db0.end(),0.0); std::fill(db1.begin(),db1.end(),0.0); }
};

// backprop for one sample, accumulate into grads
static void backward_one(const MLPParams& p,
                         const double* x,
                         int target,
                         const ForwardCache& c,
                         Grads& g)
{
    // output delta: (p - y_one_hot)
    std::vector<double> dz(p.out);
    for (int k=0;k<p.out;k++) dz[k] = c.p[k] - (k==target ? 1.0 : 0.0);

    // dW1, db1
    for (int k=0;k<p.out;k++) {
        g.db1[k] += dz[k];
        for (int j=0;j<p.hidden;j++) {
            g.dW1[j*p.out + k] += c.h[j]*dz[k];
        }
    }
    // backprop to hidden (pre-ReLU grad)
    std::vector<double> dh(p.hidden, 0.0);
    for (int j=0;j<p.hidden;j++) {
        double s = 0.0;
        for (int k=0;k<p.out;k++) s += p.W1[j*p.out + k]*dz[k];
        dh[j] = s * relu_grad(c.h[j]); // c.h[j] already after ReLU; grad=1 if >0
    }
    // dW0, db0
    for (int j=0;j<p.hidden;j++) {
        g.db0[j] += dh[j];
        for (int i=0;i<p.in;i++) {
            g.dW0[i*p.hidden + j] += x[i]*dh[j];
        }
    }
}

static void sgd_step(MLPParams& p, const Grads& g, double lr, double inv_bs, double l2=0.0) {
    // L2 adds lambda * w to grads (here folded into update)
    for (size_t t=0;t<p.W0.size();t++) p.W0[t] -= lr*(g.dW0[t]*inv_bs + l2*p.W0[t]);
    for (size_t t=0;t<p.W1.size();t++) p.W1[t] -= lr*(g.dW1[t]*inv_bs + l2*p.W1[t]);
    for (size_t j=0;j<p.b0.size();j++) p.b0[j] -= lr*(g.db0[j]*inv_bs);
    for (size_t k=0;k<p.b1.size();k++) p.b1[k] -= lr*(g.db1[k]*inv_bs);
}

struct Metrics { double loss=0.0; double acc=0.0; };

// evaluate loss/acc
static Metrics evaluate(const MLPParams& p, const Dataset& ds) {
    Metrics m; double total=0.0; size_t correct=0;
    std::vector<double> probs(p.out);
    for (size_t i=0;i<ds.n;i++) {
        const double* x = &ds.X[i*ds.d];
        auto c = forward_one(p, x);
        total += xent(c.p, ds.y[i]);
        int pred = int(std::max_element(c.p.begin(), c.p.end()) - c.p.begin());
        if (pred == ds.y[i]) correct++;
    }
    m.loss = total/ds.n;
    m.acc  = double(correct)/double(ds.n);
    return m;
}

// train loop (mini-batch SGD)
static Metrics train_mlp(MLPParams& p,
                         const Dataset& train,
                         size_t epochs=1,
                         size_t batch=16,
                         double lr=0.05,
                         double l2=0.0,
                         unsigned seed=777)
{
    std::mt19937_64 rng(seed);
    std::vector<size_t> order(train.n);
    std::iota(order.begin(), order.end(), 0);

    Grads grads(p.in, p.hidden, p.out);

    double running_loss = 0.0;
    size_t running_cnt = 0, running_correct = 0;

    for (size_t ep=0; ep<epochs; ++ep) {
        std::shuffle(order.begin(), order.end(), rng);

        for (size_t start=0; start<train.n; start+=batch) {
            size_t end = std::min(start+batch, train.n);
            size_t bs  = end - start;

            grads.zero();

            for (size_t u=start; u<end; ++u) {
                size_t i = order[u];
                const double* x = &train.X[i*train.d];
                auto c = forward_one(p, x);
                running_loss += xent(c.p, train.y[i]);
                int pred = int(std::max_element(c.p.begin(), c.p.end()) - c.p.begin());
                if (pred == train.y[i]) running_correct++;
                running_cnt++;

                backward_one(p, x, train.y[i], c, grads);
            }

            sgd_step(p, grads, lr, 1.0/double(bs), l2);
        }
    }
    Metrics m;
    m.loss = running_loss / std::max<size_t>(running_cnt,1);
    m.acc  = double(running_correct) / std::max<size_t>(running_cnt,1);
    return m;
}


class CustomGrpcClient : public GrpcClient {
    public:
        // pass a unique seed (e.g., your --client_id) when constructing
        explicit CustomGrpcClient(std::shared_ptr<ChannelInterface> channel,
                                  std::uint64_t client_seed)
            : GrpcClient(channel), client_seed_(client_seed) {}
    
        void train(const std::string& inModelPath, const std::string& outModelPath) override {
            std::cout << "USER-DEFINED CODE: Training...\n";
    
            // Load current model
            MLPParams p = load_npz_mlp_via_unzip(inModelPath);
    
            // Derive deterministic, per-client seeds
            const unsigned ds_seed  = static_cast<unsigned>(splitmix64(client_seed_));                         // dataset gen
            const unsigned opt_seed = static_cast<unsigned>(splitmix64(client_seed_ ^ 0x9E3779B97F4A7C15ULL)); // optimizer/shuffle
    
            // Small synthetic dataset, per-client different
            const size_t n_train = 80;
            Dataset train_ds = make_classification_like(n_train, p.in, ds_seed);
    
            // Light training
            Metrics tm = train_mlp(p, train_ds,
                                   /*epochs=*/2,
                                   /*batch=*/16,
                                   /*lr=*/0.05,
                                   /*l2=*/1e-4,
                                   /*seed=*/opt_seed);
    
            // Save updated weights
            save_npz_mlp(outModelPath, p);
    
            // Log metrics
            this->logMetrics({{"train_loss", tm.loss}, {"train_accuracy", tm.acc}});
        }
    
        void validate(const std::string& inModelPath, const std::string& outMetricPath) override {
            std::cout << "USER-DEFINED CODE: Validating...\n";
    
            MLPParams p = load_npz_mlp_via_unzip(inModelPath);
    
            // Independent per-client validation seed
            const unsigned val_seed = static_cast<unsigned>(splitmix64(client_seed_ ^ 0xA5A5A5A5A5A5A5A5ULL));
    
            const size_t n_val = 200;
            Dataset val_ds = make_classification_like(n_val, p.in, val_seed);
    
            Metrics vm = evaluate(p, val_ds);
    
            json m = {{"accuracy", vm.acc}, {"loss", vm.loss}};
            saveMetricsToFile(m, outMetricPath);
        }
    
        void predict(const std::string& modelPath, const std::string& outputPath) override {
            std::cout << "USER-DEFINED CODE: Predicting...\n";
            (void)modelPath;
            json p = {{"prediction", 1}, {"confidence", 0.95}};
            saveMetricsToFile(p, outputPath);
        }
    
    private:
        std::uint64_t client_seed_;
    
        // SplitMix64 hash: good for turning ids into well-distributed seeds
        static std::uint64_t splitmix64(std::uint64_t x) {
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31);
        }
    };
    

static void write_seed_npz(const std::string& path) {
    auto seed = make_seed(/*in=*/4, /*hidden=*/100, /*out=*/2);
    save_npz_mlp(path, seed);
}

// 64-bit ID from a random UUID v4
static std::uint64_t id_gen() {
    auto u = boost::uuids::random_generator()();
    std::uint64_t hi = 0, lo = 0;
    std::memcpy(&hi, u.data + 0, 8);
    std::memcpy(&lo, u.data + 8, 8);
    return hi ^ lo;
}

struct NodeportOverride {
    std::string host;
    int port = -1;
    bool enabled() const { return !host.empty() && port > 0; }
};


int main(int argc, char** argv) {
    write_seed_npz("/Users/sigvard/Desktop/fedn-cpp-client/seed.npz");

    fedn::ClientOptions opts;
    opts.discover_host = "";
    opts.token         = "";
    opts.name          = "client";
    opts.client_id     = id_gen();

    NodeportOverride np;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg.rfind("--discover_host=",0)==0) opts.discover_host = arg.substr(16);
        else if (arg.rfind("--token=",0)==0)         opts.token         = arg.substr(8);
        else if (arg.rfind("--name=",0)==0)          opts.name          = arg.substr(7);
        else if (arg.rfind("--client_id=",0)==0)     opts.client_id     = std::stoull(arg.substr(12));
        else if (arg.rfind("--node_ip=",0)==0)       np.host            = arg.substr(10);
        else if (arg.rfind("--node_port=",0)==0)     np.port            = std::stoi(arg.substr(12));
    }

    if (opts.discover_host.empty() || opts.token.empty()) {
        std::cerr << "Usage: ./my-client --discover_host=... --token=..."
                  << " [--name=...] [--client_id=...] [--node_ip=...] [--node_port=...]\n";
        return 2;
    }

    std::cout << "Starting client name=" << opts.name
              << " id=" << opts.client_id << "\n";

    fedn::FednClient cli(opts);

    // 1) Register & get combiner from API (normal path)
    auto comb = cli.getCombinerConfig();

    // 2) Override transport to NodeIP:NodePort if provided
    if (np.enabled()) {
        comb.fqdn.clear();          // ensure no TLS/Ingress path
        comb.host = np.host;
        comb.port = np.port;
    }

    // 3) Build channel. If your helper chooses TLS when fqdn is set, this will be plaintext for NodePort.
    auto chan = cli.setupGrpcChannel(comb);
    if (!chan) {
        std::cerr << "Failed to set up gRPC channel—will exit.\n";
        return 3;
    }

    auto grpc = std::make_shared<CustomGrpcClient>(chan, /*client_seed=*/opts.client_id);
    cli.run(grpc);

    return 0;
}
