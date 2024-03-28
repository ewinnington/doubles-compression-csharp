#include <iostream>
#include <bitset>
#include <vector>

#define BITS_LEADING_LENGTH 6 //Contrary to the paper claims, I need 6 bits to encode the leading zeros and not 5 bits
#define BITS_XOR_LENGTH 6

//Implementing the Xor Compression for double data as described in the Facebook Gorilla paper
//https://www.vldb.org/pvldb/vol8/p1816-teller.pdf
//Illustrated here https://www.timescale.com/blog/time-series-compression-algorithms-explained/
//Also very interesting for a follow-up https://github.com/andybbruno/TSXor

void push_back_value(std::vector<bool>& vect, const int bit_length, const int value)
{
    std::bitset<16> bs16(value);
    for(int i =bit_length-1; i>=0 ;i--)
    {   
        vect.push_back(bs16[i]);
    } 
}

void push_back_meaningful_xor(std::vector<bool>& vect, const int leading_zeros, const int trailing_zeros, const uint64_t value)
{
    std::bitset<64> bs64(value);
    for(int i = 63-leading_zeros; i>=trailing_zeros;i--) 
    {
        vect.push_back(bs64[i]); 
    }
}

int compress_gorilla(std::vector<bool>& vect, double input[], int length)
{
    uint64_t result_prev = 0xffffffffffffffff; 
    int prev_leading_zeros = 0;
    int prev_trailing_zeros = 0; 

    push_back_meaningful_xor(vect, 0, 0,  *(uint64_t*)&input[0] ); //inserting first value

    for(int i = 1; i < length; i++)
    { 
            int current_size = vect.size(); 
            char mode; 

            uint64_t val_dbl_curr = *((uint64_t* )&input[i]);
            uint64_t val_dbl_prev =  *((uint64_t* )&input[i-1]);
            uint64_t result =  (val_dbl_curr ^ val_dbl_prev);

            int leading_zeros = __builtin_clzll(result);  //thank clang++ for these functions
            int trailing_zeros =  __builtin_ctzll(result); 
           

            if (result == 0) 
            {
                vect.push_back(false);  //0: identical value
            } 
            else 
            {
                vect.push_back(true); //1: control bit
                // If the block of meaningful bits falls within the block of previous meaningful bits
                // there are at least as many leading zeros and as many trailing zeros as with the previous value
                if ((leading_zeros >= prev_leading_zeros) & (trailing_zeros == prev_trailing_zeros)) 
                {
                    vect.push_back(false);  //0: Store following meaningful XOR value
                    push_back_meaningful_xor(vect, leading_zeros, trailing_zeros, result); //use bitset to store meaningful XOR 
                }
                else
                {
                    vect.push_back(true);  //1: Store following meaningful XOR value
                    push_back_value(vect, BITS_LEADING_LENGTH, leading_zeros);//6 bits length of leading 
                    push_back_value(vect, BITS_XOR_LENGTH, 64-(leading_zeros + trailing_zeros)); //6 bits length of meaningful XOR 
                    push_back_meaningful_xor(vect, leading_zeros, trailing_zeros, result); //meaningful XOR 
                }
            }
            result_prev = result; 
            prev_leading_zeros = leading_zeros;
            prev_trailing_zeros = trailing_zeros; 
    }

    return vect.size();
}

void decompress_gorilla(std::vector<bool>& bitvector, std::vector<double>& o) 
{
    //1) Read first 64 bit as double
    std::bitset<64> bs64;
    for (int i = 0; i<64; i++)
    {
        bs64[63-i] = bitvector[i];
    }
    uint64_t v = bs64.to_ullong(); 

    o.push_back(*(double*)&v);

    //2) read the contol bit 
        //2a) 0: same value repeats
        //2b) 1: 
        //3) read encoding bit 
        //3a) 0: use last leading bits and XOR value length bits and read meaningful XOR value
        //3n) 1: read 5 bits leading length, 6 bits XOR value length and read meaningful XOR value
    
    int output_index = 1; 

    uint last_leading_bits = 0; 
    uint last_xor_bits_length = 0; 
    uint64_t last_value = v; //necessary to initialize last value 

    for(int bitvector_index = 64; bitvector_index < bitvector.size(); )
    {
        if(bitvector[bitvector_index] == 0) 
        {
            bitvector_index++;
            o.push_back(o[output_index-1]); 
        }
        else 
            {
                bitvector_index++;
                if(bitvector[bitvector_index] == 0)
                {
                    bitvector_index++;
                    std::bitset<64> value(0);
                    for (int i = 0; i<last_xor_bits_length; i++)
                    {
                        value[63-(last_leading_bits+i)] = bitvector[bitvector_index+i];
                    }
                    bitvector_index += last_xor_bits_length; 
                    uint64_t encoded = value.to_ullong(); 
                    uint64_t result = last_value ^ encoded; 
                    last_value = result; 
                    o.push_back( *(double*)&result); 
                }
                else 
                {
                    bitvector_index++;
                    std::bitset<16> lb(0); 
                    std::bitset<16> xb(0);
                    std::bitset<64> value(0);
                    for (int i = 0; i<BITS_LEADING_LENGTH; i++)
                    {
                        lb[BITS_LEADING_LENGTH-(i+1)] = bitvector[bitvector_index+i];
                    }
                    bitvector_index += BITS_LEADING_LENGTH; 

                    for (int i = 0; i<BITS_XOR_LENGTH; i++)
                    {
                        xb[BITS_XOR_LENGTH-(i+1)] = bitvector[bitvector_index+i];
                    }
                    bitvector_index += BITS_XOR_LENGTH; 

                    last_leading_bits = lb.to_ulong(); 
                    last_xor_bits_length = xb.to_ulong(); 

                    for (int i = 0; i<last_xor_bits_length; i++)
                    {
                        value[63-(last_leading_bits+i)] = bitvector[bitvector_index+i];
                    }
                    bitvector_index += last_xor_bits_length; 
                    uint64_t encoded = value.to_ullong(); 
                    uint64_t result = last_value ^ encoded; 
                    last_value = result; 
                    o.push_back( *(double*)&result); 

                    //std::cout << "lb " << lb << " xb " << xb << " value " << value << " llb " << last_leading_bits << " lxbl " << last_xor_bits_length << " result " << result << "\n"; 
                }
            }
        
        output_index += 1; 
    }
}

int main(int argc, char *argv[])
{
    const int nInput = 24; 
    double a[nInput] = { 12, 24, 15, 15.5, 14.0, 14.0, 16.0, 16.5, 18, 18, 18, 18, 20, 18, 14.0, 16.0, 16.0, 16.5, 18, 18, 18, 18, 20, 18,  }; 

    std::vector<bool> bitvector;  
    int compressed_length_bits = compress_gorilla(bitvector, a, nInput); 

    std::cout << "Length : " << compressed_length_bits << "\n";
    for (std::vector<bool>::iterator it = bitvector.begin() ; it != bitvector.end(); ++it)
        std::cout << ( *it ? "1" : "0");
    std::cout << "\n";

    std::vector<double> output; 
    decompress_gorilla(bitvector, output); 

    // ouput points that don't match
    for (int i = 0; i < nInput; i++)
    {
        if(a[i] != output[i]) std::cout  << "Mismatch " << "i : " << i << " : " << a[i] << " = " << output[i] << "\n"; 
    }

    std::cout << "Average bits per double: " << (double)(output.size() * sizeof(double) * 8 ) / compressed_length_bits << "\n";

    return 0; 
}