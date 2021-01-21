#include <eosio.token/eosio.token.hpp>
#include <sx.utils/utils.hpp>
#include <sx.curve/curve.hpp>

#include "curve.sx.hpp"

using namespace std;

[[eosio::action]]
void sx::curve::test( const uint64_t amount, const uint64_t reserve_in, const uint64_t reserve_out, const uint64_t amplifier, const uint64_t fee )
{
    print ("curve::get_amount_out(",amount,"): ", Curve::get_amount_out( amount, reserve_in, reserve_out, amplifier, fee ), "\n");

    check(false, "see print");
}

/**
 * Notify contract when any token transfer notifiers relay contract
 */
[[eosio::on_notify("*::transfer")]]
void sx::curve::on_transfer( const name from, const name to, const asset quantity, const string memo )
{
    print("Received ", quantity, " from ", from, " with memo \"", memo, "\"");

    // authenticate incoming `from` account
    require_auth( from );

    // ignore transfers
    if ( to != get_self() || memo == get_self().to_string() || from == "eosio.ram"_n) return;

    // settings
    sx::curve::settings _settings( get_self(), get_self().value );
    sx::curve::pairs _pairs( get_self(), get_self().value );
    check( _settings.exists(), "contract is under maintenance");
    auto settings = _settings.get();

    // TEMP - DURING TESTING PERIOD
    check( from.suffix() == "sx"_n || from == "myaccount"_n, "account must be *.sx");

    // user input params
    const name contract = get_first_receiver();
    const auto [ min_ext_out, receiver ] = parse_memo(memo);
    const symbol_code memo_symcode = min_ext_out.quantity.symbol.code();
    const symbol_code pair_id = find_pair_id( quantity.symbol.code(), memo_symcode );

    // check incoming transfer
    const auto& pairs = _pairs.get( pair_id.raw(), "pair id does not exist");
    const bool is_in = pairs.reserve0.quantity.symbol == quantity.symbol;
    const extended_asset reserve_in = is_in ? pairs.reserve0 : pairs.reserve1;
    const extended_asset reserve_out = is_in ? pairs.reserve1 : pairs.reserve0;
    const symbol sym_in = reserve_in.quantity.symbol;
    const symbol sym_out = reserve_out.quantity.symbol;
    check( reserve_in.contract == contract, "reserve_in contract mismatch");
    check( sym_in == quantity.symbol, "reserve_in symbol mismatch");

    // max precision
    const uint8_t max_precision = max( sym_in.precision(), reserve_out.quantity.symbol.precision() );

    // calculate out
    const int64_t amount_out = Curve::get_amount_out(
        mul_amount( quantity.amount, max_precision, quantity.symbol.precision() ),
        mul_amount( reserve_in.quantity.amount, max_precision, sym_in.precision() ),
        mul_amount( reserve_out.quantity.amount, max_precision, sym_out.precision() ),
        pairs.amplifier,
        settings.fee
    );
    const extended_asset out = { div_amount( amount_out, max_precision, sym_out.precision() ), reserve_out.get_extended_symbol() };

    check(min_ext_out.contract.value == 0 || min_ext_out.contract == reserve_out.contract, "reserve_out vs memo contract mismatch");
    check(min_ext_out.quantity.amount == 0 || min_ext_out.quantity.symbol == out.quantity.symbol, "return vs memo symbol precision mismatch");
    check(min_ext_out.quantity.amount == 0 || min_ext_out.quantity.amount <= amount_out, "return is not enough");

    // modify reserves
    _pairs.modify( pairs, get_self(), [&]( auto & row ) {
        if ( is_in ) {
            row.reserve0.quantity += quantity;
            row.reserve1.quantity -= out.quantity;
        } else {
            row.reserve0.quantity -= out.quantity;
            row.reserve1.quantity += quantity;
        }
        // calculate last price
        const double price = static_cast<double>(quantity.amount) / out.quantity.amount;
        row.price0_last = is_in ? 1 / price : price;
        row.price1_last = is_in ? price : 1 / price;

        // calculate incoming culmative trading volume
        row.volume0 += is_in ? quantity.amount : 0;
        row.volume1 += is_in ? 0 : quantity.amount;
        row.last_updated = current_time_point();
    });

    // transfer amount to sender
    transfer( get_self(), receiver.value ? receiver : from, out, "swap" );
}

int64_t sx::curve::mul_amount( const int64_t amount, const uint8_t precision0, const uint8_t precision1 )
{
    return amount * pow(10, precision0 - precision1 );
}

int64_t sx::curve::div_amount( const int64_t amount, const uint8_t precision0, const uint8_t precision1 )
{
    return amount / pow(10, precision0 - precision1 );
}

pair<extended_asset, name> sx::curve::parse_memo(string memo){

    name receiver;
    auto arr = sx::utils::split(memo, ",");

    check(arr.size() < 3 && arr.size() > 0, "invalid memo format");
    if(arr.size()==2) {
        receiver = sx::utils::parse_name(arr[1]);
        check(receiver.value, "invalid receiver name in memo");
        check(is_account(receiver), "receiver doesn't exist on the blockchain");
    }

    memo = arr[0];

    auto sym_code = sx::utils::parse_symbol_code(memo);
    if (sym_code.is_valid()) return { extended_asset{ asset{0, symbol{sym_code, 0} }, ""_n}, receiver };

    auto quantity = sx::utils::parse_asset(memo);
    if (quantity.is_valid()) return { extended_asset{quantity, ""_n}, receiver };

    auto ext_out = sx::utils::parse_extended_asset(memo);
    if (ext_out.quantity.is_valid()) return { ext_out, receiver };

    check(false, "invalid memo");
    return {};
}

symbol_code sx::curve::find_pair_id( const symbol_code in_symcode, const symbol_code memo_symcode )
{
    check( in_symcode != memo_symcode, "memo symbol must not match quantity symbol");

    sx::curve::pairs _pairs( get_self(), get_self().value );

    // find by input quantity
    auto itr0 = _pairs.find( in_symcode.raw() );
    if ( itr0 != _pairs.end() ) return itr0->id;

    // find by memo symbol
    auto itr1 = _pairs.find( memo_symcode.raw() );
    if ( itr1 != _pairs.end() ) return itr1->id;

    // find by combination of input quantity & memo symbol
    auto _pairs_by_reserves = _pairs.get_index<"byreserves"_n>();
    auto itr = _pairs_by_reserves.find( compute_by_symcodes( in_symcode, memo_symcode ) );
    if ( itr != _pairs_by_reserves.end() ) return itr->id;

    itr = _pairs_by_reserves.find( compute_by_symcodes( memo_symcode, in_symcode ) );
    if ( itr != _pairs_by_reserves.end() ) return itr->id;

    check( false, "cannot find pair id for input quantity and memo");
    return {};
}

[[eosio::action]]
void sx::curve::setsettings( const std::optional<sx::curve::settings_row> settings )
{
    require_auth( get_self() );
    sx::curve::settings _settings( get_self(), get_self().value );

    // clear table if setting is `null`
    if ( !settings ) return _settings.remove();

    _settings.set( *settings, get_self() );
}

[[eosio::action]]
void sx::curve::setpair( const symbol_code id, const extended_asset reserve0, const extended_asset reserve1, const uint64_t amplifier )
{
    require_auth( get_self() );
    sx::curve::pairs _pairs( get_self(), get_self().value );

    // reserve params
    const name contract0 = reserve0.contract;
    const name contract1 = reserve1.contract;
    const symbol sym0 = reserve0.quantity.symbol;
    const symbol sym1 = reserve1.quantity.symbol;
    const uint8_t max_precision = max( sym0.precision(), sym1.precision() );

    // check reserves
    check( is_account( contract0 ), "reserve0 contract does not exists");
    check( is_account( contract1 ), "reserve1 contract does not exists");
    check( token::get_supply( contract0, sym0.code() ).symbol == sym0, "reserve0 symbol mismatch" );
    check( token::get_supply( contract1, sym1.code() ).symbol == sym1, "reserve1 symbol mismatch" );
    check( mul_amount(reserve0.quantity.amount, max_precision, sym0.precision()) == mul_amount(reserve1.quantity.amount, max_precision, sym1.precision()), "reserve0 & reserve1 amount must match");

    // pairs content
    auto insert = [&]( auto & row ) {
        row.id = id;
        row.reserve0 = reserve0;
        row.reserve1 = reserve1;
        row.liquidity = {asset{0, { id, 4 }}, get_self() };
        row.amplifier = amplifier;
        row.last_updated = current_time_point();
    };

    // create/modify pairs
    auto itr = _pairs.find( id.raw() );
    if ( itr == _pairs.end() ) _pairs.emplace( get_self(), insert );
    else _pairs.modify( itr, get_self(), insert );
}

[[eosio::action]]
void sx::curve::reset()
{
    require_auth( get_self() );

    sx::curve::pairs _pairs( get_self(), get_self().value );
    sx::curve::settings _settings( get_self(), get_self().value );

    _settings.remove();
    clear_table( _pairs );
}

template <typename T>
void sx::curve::clear_table( T& table )
{
    auto itr = table.begin();
    while ( itr != table.end() ) {
        itr = table.erase( itr );
    }
}

void sx::curve::issue( const extended_asset value, const string memo )
{
    eosio::token::issue_action issue( value.contract, { get_self(), "active"_n });
    issue.send( get_self(), value.quantity, memo );
}

void sx::curve::retire( const extended_asset value, const string memo )
{
    eosio::token::retire_action retire( value.contract, { get_self(), "active"_n });
    retire.send( value.quantity, memo );
}

void sx::curve::transfer( const name from, const name to, const extended_asset value, const string memo )
{
    eosio::token::transfer_action transfer( value.contract, { from, "active"_n });
    transfer.send( from, to, value.quantity, memo );
}
